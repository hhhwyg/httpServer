#include "httpserver/application/ChatService.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <limits>
#include <mutex>
#include <chrono>
#include <unordered_map>
#include <utility>

#include "httpserver/application/InMemoryChatRepository.h"
#include "httpserver/application/AuthenticationRateLimiter.h"
#include "httpserver/application/RedisClient.h"
#include "httpserver/application/Metrics.h"
#include "httpserver/application/StructuredLog.h"
#include "httpserver/protocol/ChatProtocol.h"
#include "json.hpp"

namespace httpserver {
namespace {

bool IsValidRoomName(std::string_view name) {
  if (name.empty() || name.size() > 128) return false;
  return std::any_of(name.begin(), name.end(),
                     [](unsigned char character) { return std::isspace(character) == 0; });
}

bool IsValidIdentifier(std::string_view value) {
  return !value.empty() && value.size() <= 128;
}

std::optional<std::uint64_t> ParseCursor(std::string_view cursor) {
  if (cursor.empty()) return std::numeric_limits<std::uint64_t>::max();
  std::uint64_t value = 0;
  const auto result = std::from_chars(cursor.data(), cursor.data() + cursor.size(), value);
  if (result.ec != std::errc{} || result.ptr != cursor.data() + cursor.size() || value == 0) {
    return std::nullopt;
  }
  return value;
}

std::optional<std::uint64_t> ParseAfterCursor(std::string_view cursor) {
  if (cursor.empty()) return std::uint64_t{0};
  std::uint64_t value = 0;
  const auto result = std::from_chars(cursor.data(), cursor.data() + cursor.size(), value);
  if (result.ec != std::errc{} || result.ptr != cursor.data() + cursor.size()) {
    return std::nullopt;
  }
  return value;
}

}  // namespace

class ChatService::Impl {
 public:
  std::optional<Room> FindRoom(std::string_view roomId) const {
    if (const auto cached = RedisService::GetRoom(roomId); cached.has_value()) {
      RedisService::RecordCacheHit();
      return cached;
    }
    RedisService::RecordCacheMiss();
    const auto room = repository->Find(roomId);
    if (room.has_value()) RedisService::PutRoom(*room);
    return room;
  }

  RoomSummary Summary(const Room& room, std::string_view userId) const {
    return RoomSummary{room.id, room.name, room.ownerId,
                       static_cast<const MembershipRepository&>(*repository).Count(room.id),
                       repository->Contains(room.id, userId)};
  }

  mutable std::mutex mutex;
  std::unique_ptr<ChatRepository> repository = std::make_unique<InMemoryChatRepository>();
  std::unordered_map<std::string,
                     std::unordered_map<std::uint64_t, std::weak_ptr<ConnectionSession>>>
      sessions;
  std::size_t roomCount = 0;
  bool outboxEnabled = false;
  int messageRateLimit = 1000;
  int messageRateWindowSeconds = 60;
  std::unordered_map<std::string, std::chrono::steady_clock::time_point> deliveredEvents;
};

const char* ChatServiceErrorCode(ChatServiceError error) {
  switch (error) {
    case ChatServiceError::kNone:
      return "ok";
    case ChatServiceError::kInvalidArgument:
      return "invalid_argument";
    case ChatServiceError::kRoomNotFound:
      return "room_not_found";
    case ChatServiceError::kAlreadyMember:
      return "already_room_member";
    case ChatServiceError::kNotMember:
      return "room_membership_required";
    case ChatServiceError::kOwnerCannotLeave:
      return "room_owner_cannot_leave";
    case ChatServiceError::kInvalidCursor:
      return "invalid_cursor";
    case ChatServiceError::kDependencyUnavailable:
      return "dependency_unavailable";
  }
  return "internal_error";
}

ChatService& ChatService::Instance() {
  static ChatService service;
  return service;
}

ChatService::ChatService() : impl_(new Impl) {}

ChatService::~ChatService() = default;

void ChatService::ConfigureRepository(std::unique_ptr<ChatRepository> repository) {
  if (!repository) return;
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->repository = std::move(repository);
  impl_->outboxEnabled = true;
  impl_->sessions.clear();
  // Repository replacement only occurs during bootstrap, before EventLoops
  // accept requests, so this bounded startup query cannot block a request.
  impl_->roomCount = static_cast<const RoomRepository&>(*impl_->repository).Count();
}

void ChatService::ConfigureMessageRateLimit(int maxMessages, int windowSeconds) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->messageRateLimit = maxMessages;
  impl_->messageRateWindowSeconds = windowSeconds;
}

bool ChatService::AllowMessage(std::string_view userId) const {
  int maxMessages = 0;
  int windowSeconds = 0;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    maxMessages = impl_->messageRateLimit;
    windowSeconds = impl_->messageRateWindowSeconds;
  }
  return AuthenticationRateLimiter::AllowDistributed("message:send", userId, maxMessages,
                                                     windowSeconds);
}

bool ChatService::persistenceAvailable() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->repository->available();
}

bool ChatService::outboxEnabled() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->outboxEnabled;
}

CreateRoomResult ChatService::CreateRoom(std::string_view ownerId, std::string_view name) {
  if (ownerId.empty() || !IsValidRoomName(name)) {
    return {ChatServiceError::kInvalidArgument, {}};
  }

  std::lock_guard<std::mutex> lock(impl_->mutex);
  const Room room = impl_->repository->Create(ownerId, name);
  if (room.id.empty()) return {ChatServiceError::kDependencyUnavailable, {}};
  impl_->repository->Add(room.id, ownerId);
  ++impl_->roomCount;
  RedisService::PutRoom(room);
  return {ChatServiceError::kNone, impl_->Summary(room, ownerId)};
}

std::vector<RoomSummary> ChatService::ListRooms(std::string_view userId) const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  std::vector<RoomSummary> result;
  for (const Room& room : impl_->repository->ListForMember(userId)) {
    result.push_back(impl_->Summary(room, userId));
  }
  return result;
}

ChatServiceError ChatService::JoinRoom(std::string_view userId, std::string_view roomId) {
  if (userId.empty() || roomId.empty()) {
    return ChatServiceError::kInvalidArgument;
  }
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (!impl_->FindRoom(roomId).has_value()) {
    return ChatServiceError::kRoomNotFound;
  }
  return impl_->repository->Add(roomId, userId) ? ChatServiceError::kNone
                                                : ChatServiceError::kAlreadyMember;
}

ChatServiceError ChatService::LeaveRoom(std::string_view userId, std::string_view roomId) {
  if (userId.empty() || roomId.empty()) {
    return ChatServiceError::kInvalidArgument;
  }
  std::lock_guard<std::mutex> lock(impl_->mutex);
  const auto room = impl_->FindRoom(roomId);
  if (!room.has_value()) return ChatServiceError::kRoomNotFound;
  if (room->ownerId == userId) {
    return ChatServiceError::kOwnerCannotLeave;
  }
  if (!impl_->repository->Remove(roomId, userId)) {
    return ChatServiceError::kNotMember;
  }

  auto& sessions = impl_->sessions[std::string(roomId)];
  for (auto session = sessions.begin(); session != sessions.end();) {
    const auto active = session->second.lock();
    if (!active || active->userId() == userId) {
      session = sessions.erase(session);
    } else {
      ++session;
    }
  }
  return ChatServiceError::kNone;
}

RoomMembersResult ChatService::ListMembers(std::string_view userId, std::string_view roomId) const {
  if (userId.empty() || roomId.empty()) {
    return {ChatServiceError::kInvalidArgument, {}};
  }
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (!impl_->FindRoom(roomId).has_value()) {
    return {ChatServiceError::kRoomNotFound, {}};
  }
  if (!impl_->repository->Contains(roomId, userId)) {
    return {ChatServiceError::kNotMember, {}};
  }

  RoomMembersResult result;
  for (const RoomMember& member : impl_->repository->List(roomId)) {
    result.members.push_back(member.userId);
  }
  return result;
}

SendMessageResult ChatService::SendMessage(const SendMessageCommand& command) {
  if (command.roomId.empty() || command.senderId.empty() ||
      !IsValidIdentifier(command.clientMessageId) || command.content.empty() ||
      command.content.size() > 4096 || command.senderSessionId == 0) {
    return {ChatServiceError::kInvalidArgument, {}, MessageDeliveryStage::kReceived, false, {}};
  }

  SendMessageResult result;
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (!impl_->FindRoom(command.roomId).has_value()) {
    result.error = ChatServiceError::kRoomNotFound;
    return result;
  }
  if (!impl_->repository->Contains(command.roomId, command.senderId)) {
    result.error = ChatServiceError::kNotMember;
    return result;
  }

  auto roomSessions = impl_->sessions.find(command.roomId);
  if (roomSessions == impl_->sessions.end()) {
    result.error = ChatServiceError::kNotMember;
    return result;
  }
  const auto sender = roomSessions->second.find(command.senderSessionId);
  if (sender == roomSessions->second.end() || sender->second.expired()) {
    if (sender != roomSessions->second.end()) roomSessions->second.erase(sender);
    result.error = ChatServiceError::kNotMember;
    return result;
  }
  const auto activeSender = sender->second.lock();
  if (!activeSender || activeSender->userId() != command.senderId) {
    result.error = ChatServiceError::kNotMember;
    return result;
  }

  const MessageAppendResult persisted = impl_->repository->AppendOrGet(
      command.roomId, command.senderId, command.clientMessageId, command.content);
  if (persisted.error != ChatServiceError::kNone) {
    result.error = persisted.error;
    return result;
  }
  result.message = persisted.message;
  result.inserted = persisted.inserted;
  result.stage = MessageDeliveryStage::kPersisted;
  if (result.inserted) RedisService::InvalidateRoom(command.roomId);
  if (!result.inserted) {
    return result;
  }

  for (auto session = roomSessions->second.begin(); session != roomSessions->second.end();) {
    if (const auto active = session->second.lock()) {
      if (session->first != command.senderSessionId) {
        result.recipients.push_back(active);
      }
      ++session;
    } else {
      session = roomSessions->second.erase(session);
    }
  }
  return result;
}

MessageDispatchResult ChatService::DispatchMessage(std::string_view roomId,
                                                   std::string_view senderId,
                                                   std::string_view clientMessageId,
                                                   std::string_view content,
                                                   std::uint64_t senderSessionId) {
  return SendMessage({std::string(roomId), std::string(senderId), std::string(clientMessageId),
                      std::string(content), senderSessionId});
}

MessageHistoryResult ChatService::ListMessages(std::string_view userId, std::string_view roomId,
                                               std::string_view before, std::size_t limit) const {
  if (userId.empty() || roomId.empty() || limit == 0 || limit > 100) {
    return {ChatServiceError::kInvalidArgument, {}, std::nullopt, std::nullopt};
  }
  const auto cursor = ParseCursor(before);
  if (!cursor.has_value()) {
    return {ChatServiceError::kInvalidCursor, {}, std::nullopt, std::nullopt};
  }

  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (!impl_->FindRoom(roomId).has_value()) {
    return {ChatServiceError::kRoomNotFound, {}, std::nullopt, std::nullopt};
  }
  if (!impl_->repository->Contains(roomId, userId)) {
    return {ChatServiceError::kNotMember, {}, std::nullopt, std::nullopt};
  }

  MessageHistoryResult result;
  result.messages = impl_->repository->ListBefore(roomId, *cursor, limit + 1);
  if (result.messages.size() > limit) {
    result.messages.erase(result.messages.begin());
    result.nextBefore = result.messages.front().id;
  }
  return result;
}

MessageHistoryResult ChatService::ListMessagesAfter(std::string_view userId,
                                                    std::string_view roomId, std::string_view after,
                                                    std::size_t limit) const {
  if (userId.empty() || roomId.empty() || limit == 0 || limit > 100) {
    return {ChatServiceError::kInvalidArgument, {}, std::nullopt, std::nullopt};
  }
  const auto cursor = ParseAfterCursor(after);
  if (!cursor.has_value()) {
    return {ChatServiceError::kInvalidCursor, {}, std::nullopt, std::nullopt};
  }

  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (!impl_->FindRoom(roomId).has_value()) {
    return {ChatServiceError::kRoomNotFound, {}, std::nullopt, std::nullopt};
  }
  if (!impl_->repository->Contains(roomId, userId)) {
    return {ChatServiceError::kNotMember, {}, std::nullopt, std::nullopt};
  }

  MessageHistoryResult result;
  if (*cursor == 0 && limit <= 50) {
    if (const auto cached = RedisService::GetRecentMessages(roomId); cached.has_value()) {
      RedisService::RecordCacheHit();
      result.messages = *cached;
      if (result.messages.size() > limit) {
        result.messages.resize(limit);
        result.nextAfter = result.messages.back().id;
      }
      return result;
    }
    RedisService::RecordCacheMiss();
    const std::vector<Message> recent = impl_->repository->ListAfter(roomId, 0, 50);
    RedisService::PutRecentMessages(roomId, recent);
    result.messages = recent;
    if (result.messages.size() > limit) {
      result.messages.resize(limit);
      result.nextAfter = result.messages.back().id;
    }
    return result;
  }
  result.messages = impl_->repository->ListAfter(roomId, *cursor, limit + 1);
  if (result.messages.size() > limit) {
    result.messages.pop_back();
    result.nextAfter = result.messages.back().id;
  }
  return result;
}

ChatServiceError ChatService::AttachSession(std::string_view roomId,
                                            const std::shared_ptr<ConnectionSession>& session,
                                            bool enrollIfMissing) {
  if (!session || roomId.empty() || session->userId().empty()) {
    return ChatServiceError::kInvalidArgument;
  }
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (!impl_->FindRoom(roomId).has_value()) {
    return ChatServiceError::kRoomNotFound;
  }
  if (!impl_->repository->Contains(roomId, session->userId())) {
    if (!enrollIfMissing) return ChatServiceError::kNotMember;
    impl_->repository->Add(roomId, session->userId());
  }
  impl_->sessions[std::string(roomId)][session->sessionId()] = session;
  return ChatServiceError::kNone;
}

ChatServiceError ChatService::Broadcast(std::string_view roomId, std::string_view message,
                                        std::uint64_t senderSessionId) {
  std::vector<std::shared_ptr<ConnectionSession>> targets;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const auto room = impl_->sessions.find(std::string(roomId));
    if (!impl_->FindRoom(roomId).has_value()) {
      return ChatServiceError::kRoomNotFound;
    }
    if (room == impl_->sessions.end()) return ChatServiceError::kNotMember;

    const auto sender = room->second.find(senderSessionId);
    if (sender == room->second.end()) return ChatServiceError::kNotMember;
    if (sender->second.expired()) {
      room->second.erase(sender);
      return ChatServiceError::kNotMember;
    }

    for (auto session = room->second.begin(); session != room->second.end();) {
      if (const auto active = session->second.lock()) {
        if (session->first != senderSessionId) targets.push_back(active);
        ++session;
      } else {
        session = room->second.erase(session);
      }
    }
  }

  const auto broadcastStarted = std::chrono::steady_clock::now();
  for (const auto& target : targets) target->sendMessage(message);
  Metrics::WebSocketBroadcast(
      static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                     std::chrono::steady_clock::now() - broadcastStarted)
                                     .count()));
  return ChatServiceError::kNone;
}

bool ChatService::DeliverOutboxEvent(std::string_view encodedEvent) {
  using json = nlohmann::json;
  json event;
  try {
    event = json::parse(encodedEvent);
    if (!event.is_object() || event.value("eventType", "") != "message.created" ||
        !event.contains("eventId") || !event["eventId"].is_string())
      return false;
    const std::string eventId = event["eventId"].get<std::string>();
    const Message message{
        event.at("messageId").get<std::string>(), event.at("roomId").get<std::string>(),
        event.at("senderId").get<std::string>(),  event.at("clientMessageId").get<std::string>(),
        event.at("content").get<std::string>(),   event.at("createdAt").get<std::string>()};
    std::vector<std::shared_ptr<ConnectionSession>> recipients;
    {
      std::lock_guard<std::mutex> lock(impl_->mutex);
      const auto now = std::chrono::steady_clock::now();
      for (auto it = impl_->deliveredEvents.begin(); it != impl_->deliveredEvents.end();) {
        if (now - it->second > std::chrono::minutes(10))
          it = impl_->deliveredEvents.erase(it);
        else
          ++it;
      }
      if (impl_->deliveredEvents.find(eventId) != impl_->deliveredEvents.end()) return false;
      impl_->deliveredEvents.emplace(eventId, now);
      while (impl_->deliveredEvents.size() > 10000)
        impl_->deliveredEvents.erase(impl_->deliveredEvents.begin());
      const auto room = impl_->sessions.find(message.roomId);
      if (room != impl_->sessions.end()) {
        for (const auto& entry : room->second) {
          if (const auto session = entry.second.lock()) recipients.push_back(session);
        }
      }
    }
    const auto broadcastStarted = std::chrono::steady_clock::now();
    const std::string push = ChatProtocol::MessageCreated(message);
    for (const auto& recipient : recipients) recipient->sendMessage(push);
    const auto duration =
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                       std::chrono::steady_clock::now() - broadcastStarted)
                                       .count());
    Metrics::WebSocketBroadcast(duration);
    LogStructuredEvent({message.clientMessageId,
                        {},
                        eventId,
                        message.id,
                        "websocket.broadcast",
                        {},
                        duration / 1000});
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

void ChatService::DetachSession(std::uint64_t sessionId) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  for (auto& room : impl_->sessions) room.second.erase(sessionId);
}

std::size_t ChatService::RoomCount() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return static_cast<const RoomRepository&>(*impl_->repository).Count();
}

std::size_t ChatService::CachedRoomCount() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->roomCount;
}

void ChatService::ResetForTesting() {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->repository = std::make_unique<InMemoryChatRepository>();
  impl_->sessions.clear();
  impl_->roomCount = 0;
  impl_->outboxEnabled = false;
}

}  // namespace httpserver
