#include "httpserver/application/ChatService.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <utility>

#include "httpserver/application/InMemoryChatRepository.h"

namespace httpserver {
namespace {

bool IsValidRoomName(std::string_view name) {
  if (name.empty() || name.size() > 128) return false;
  return std::any_of(name.begin(), name.end(), [](unsigned char character) {
    return std::isspace(character) == 0;
  });
}

bool IsValidIdentifier(std::string_view value) {
  return !value.empty() && value.size() <= 128;
}

std::optional<std::uint64_t> ParseCursor(std::string_view cursor) {
  if (cursor.empty()) return std::numeric_limits<std::uint64_t>::max();
  std::uint64_t value = 0;
  const auto result =
      std::from_chars(cursor.data(), cursor.data() + cursor.size(), value);
  if (result.ec != std::errc{} || result.ptr != cursor.data() + cursor.size() ||
      value == 0) {
    return std::nullopt;
  }
  return value;
}

}  // namespace

class ChatService::Impl {
 public:
  RoomSummary Summary(const Room& room, std::string_view userId) const {
    return RoomSummary{room.id,
                       room.name,
                       room.ownerId,
                       repository.Count(room.id),
                       repository.Contains(room.id, userId)};
  }

  mutable std::mutex mutex;
  InMemoryChatRepository repository;
  std::unordered_map<
      std::string,
      std::unordered_map<std::uint64_t, std::weak_ptr<ConnectionSession>>>
      sessions;
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
    case ChatServiceError::kDuplicateMessage:
      return "duplicate_message";
    case ChatServiceError::kInvalidCursor:
      return "invalid_cursor";
  }
  return "internal_error";
}

ChatService& ChatService::Instance() {
  static ChatService service;
  return service;
}

ChatService::ChatService() : impl_(new Impl) {}

ChatService::~ChatService() = default;

CreateRoomResult ChatService::CreateRoom(std::string_view ownerId,
                                         std::string_view name) {
  if (ownerId.empty() || !IsValidRoomName(name)) {
    return {ChatServiceError::kInvalidArgument, {}};
  }

  std::lock_guard<std::mutex> lock(impl_->mutex);
  const Room room = impl_->repository.Create(ownerId, name);
  impl_->repository.Add(room.id, ownerId);
  return {ChatServiceError::kNone, impl_->Summary(room, ownerId)};
}

std::vector<RoomSummary> ChatService::ListRooms(
    std::string_view userId) const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  std::vector<RoomSummary> result;
  for (const Room& room : impl_->repository.ListForMember(userId)) {
    result.push_back(impl_->Summary(room, userId));
  }
  return result;
}

ChatServiceError ChatService::JoinRoom(std::string_view userId,
                                       std::string_view roomId) {
  if (userId.empty() || roomId.empty()) {
    return ChatServiceError::kInvalidArgument;
  }
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (!impl_->repository.Find(roomId).has_value()) {
    return ChatServiceError::kRoomNotFound;
  }
  return impl_->repository.Add(roomId, userId)
             ? ChatServiceError::kNone
             : ChatServiceError::kAlreadyMember;
}

ChatServiceError ChatService::LeaveRoom(std::string_view userId,
                                        std::string_view roomId) {
  if (userId.empty() || roomId.empty()) {
    return ChatServiceError::kInvalidArgument;
  }
  std::lock_guard<std::mutex> lock(impl_->mutex);
  const auto room = impl_->repository.Find(roomId);
  if (!room.has_value()) return ChatServiceError::kRoomNotFound;
  if (room->ownerId == userId) {
    return ChatServiceError::kOwnerCannotLeave;
  }
  if (!impl_->repository.Remove(roomId, userId)) {
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

RoomMembersResult ChatService::ListMembers(std::string_view userId,
                                           std::string_view roomId) const {
  if (userId.empty() || roomId.empty()) {
    return {ChatServiceError::kInvalidArgument, {}};
  }
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (!impl_->repository.Find(roomId).has_value()) {
    return {ChatServiceError::kRoomNotFound, {}};
  }
  if (!impl_->repository.Contains(roomId, userId)) {
    return {ChatServiceError::kNotMember, {}};
  }

  RoomMembersResult result;
  for (const RoomMember& member : impl_->repository.List(roomId)) {
    result.members.push_back(member.userId);
  }
  return result;
}

MessageDispatchResult ChatService::DispatchMessage(
    std::string_view roomId, std::string_view senderId,
    std::string_view clientMessageId, std::string_view content,
    std::uint64_t senderSessionId) {
  if (roomId.empty() || senderId.empty() ||
      !IsValidIdentifier(clientMessageId) || content.empty() ||
      content.size() > 4096) {
    return {ChatServiceError::kInvalidArgument, {}, {}};
  }

  MessageDispatchResult result;
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (!impl_->repository.Find(roomId).has_value()) {
    result.error = ChatServiceError::kRoomNotFound;
    return result;
  }
  if (!impl_->repository.Contains(roomId, senderId)) {
    result.error = ChatServiceError::kNotMember;
    return result;
  }

  auto roomSessions = impl_->sessions.find(std::string(roomId));
  if (roomSessions == impl_->sessions.end()) {
    result.error = ChatServiceError::kNotMember;
    return result;
  }
  const auto sender = roomSessions->second.find(senderSessionId);
  if (sender == roomSessions->second.end() || sender->second.expired()) {
    if (sender != roomSessions->second.end()) roomSessions->second.erase(sender);
    result.error = ChatServiceError::kNotMember;
    return result;
  }
  const auto activeSender = sender->second.lock();
  if (!activeSender || activeSender->userId() != senderId) {
    result.error = ChatServiceError::kNotMember;
    return result;
  }

  if (const auto duplicate = impl_->repository.FindByClientId(
          roomId, senderId, clientMessageId);
      duplicate.has_value()) {
    result.error = ChatServiceError::kDuplicateMessage;
    result.message = *duplicate;
    return result;
  }

  result.message =
      impl_->repository.Append(roomId, senderId, clientMessageId, content);
  for (auto session = roomSessions->second.begin();
       session != roomSessions->second.end();) {
    if (const auto active = session->second.lock()) {
      if (session->first != senderSessionId) result.recipients.push_back(active);
      ++session;
    } else {
      session = roomSessions->second.erase(session);
    }
  }
  return result;
}

MessageHistoryResult ChatService::ListMessages(
    std::string_view userId, std::string_view roomId, std::string_view before,
    std::size_t limit) const {
  if (userId.empty() || roomId.empty() || limit == 0 || limit > 100) {
    return {ChatServiceError::kInvalidArgument, {}, std::nullopt};
  }
  const auto cursor = ParseCursor(before);
  if (!cursor.has_value()) {
    return {ChatServiceError::kInvalidCursor, {}, std::nullopt};
  }

  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (!impl_->repository.Find(roomId).has_value()) {
    return {ChatServiceError::kRoomNotFound, {}, std::nullopt};
  }
  if (!impl_->repository.Contains(roomId, userId)) {
    return {ChatServiceError::kNotMember, {}, std::nullopt};
  }

  MessageHistoryResult result;
  result.messages = impl_->repository.ListBefore(roomId, *cursor, limit + 1);
  if (result.messages.size() > limit) {
    result.messages.erase(result.messages.begin());
    result.nextBefore = result.messages.front().id;
  }
  return result;
}

ChatServiceError ChatService::AttachSession(
    std::string_view roomId,
    const std::shared_ptr<ConnectionSession>& session,
    bool enrollIfMissing) {
  if (!session || roomId.empty() || session->userId().empty()) {
    return ChatServiceError::kInvalidArgument;
  }
  std::lock_guard<std::mutex> lock(impl_->mutex);
  if (!impl_->repository.Find(roomId).has_value()) {
    return ChatServiceError::kRoomNotFound;
  }
  if (!impl_->repository.Contains(roomId, session->userId())) {
    if (!enrollIfMissing) return ChatServiceError::kNotMember;
    impl_->repository.Add(roomId, session->userId());
  }
  impl_->sessions[std::string(roomId)][session->sessionId()] = session;
  return ChatServiceError::kNone;
}

ChatServiceError ChatService::Broadcast(std::string_view roomId,
                                        std::string_view message,
                                        std::uint64_t senderSessionId) {
  std::vector<std::shared_ptr<ConnectionSession>> targets;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const auto room = impl_->sessions.find(std::string(roomId));
    if (!impl_->repository.Find(roomId).has_value()) {
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

  for (const auto& target : targets) target->sendMessage(message);
  return ChatServiceError::kNone;
}

void ChatService::DetachSession(std::uint64_t sessionId) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  for (auto& room : impl_->sessions) room.second.erase(sessionId);
}

std::size_t ChatService::RoomCount() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->repository.Count();
}

void ChatService::ResetForTesting() {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->repository.Reset();
  impl_->sessions.clear();
}

}  // namespace httpserver
