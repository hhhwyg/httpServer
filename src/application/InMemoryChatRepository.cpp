#include "httpserver/application/InMemoryChatRepository.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace httpserver {
namespace {

std::string UtcTimestamp() {
  const auto now = std::chrono::system_clock::now();
  const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
                                now.time_since_epoch()) %
                            1000;
  const std::time_t time = std::chrono::system_clock::to_time_t(now);
  std::tm utc{};
  gmtime_r(&time, &utc);
  std::ostringstream output;
  output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S") << '.'
         << std::setfill('0') << std::setw(3) << milliseconds.count() << 'Z';
  return output.str();
}

}  // namespace

class InMemoryChatRepository::Impl {
 public:
  mutable std::mutex mutex;
  std::unordered_map<std::string, Room> rooms;
  std::unordered_map<std::string, std::unordered_set<std::string>> members;
  std::unordered_map<std::string, std::vector<Message>> messages;
  std::unordered_map<std::string,
                     std::unordered_map<std::string, Message>>
      messagesByClientId;
  std::uint64_t nextRoomId = 10000000;
  std::uint64_t nextMessageId = 20000000;
};

InMemoryChatRepository::InMemoryChatRepository() : impl_(new Impl) {}

InMemoryChatRepository::~InMemoryChatRepository() = default;

Room InMemoryChatRepository::Create(std::string_view ownerId,
                                    std::string_view name) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  Room room{std::to_string(impl_->nextRoomId++), std::string(name),
            std::string(ownerId)};
  impl_->rooms.emplace(room.id, room);
  return room;
}

std::optional<Room> InMemoryChatRepository::Find(
    std::string_view roomId) const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  const auto room = impl_->rooms.find(std::string(roomId));
  return room == impl_->rooms.end() ? std::nullopt
                                    : std::optional<Room>(room->second);
}

std::vector<Room> InMemoryChatRepository::ListForMember(
    std::string_view userId) const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  std::vector<Room> rooms;
  for (const auto& entry : impl_->rooms) {
    const auto members = impl_->members.find(entry.first);
    if (members != impl_->members.end() &&
        members->second.find(std::string(userId)) != members->second.end()) {
      rooms.push_back(entry.second);
    }
  }
  std::sort(rooms.begin(), rooms.end(),
            [](const Room& left, const Room& right) {
              return left.id < right.id;
            });
  return rooms;
}

std::size_t InMemoryChatRepository::Count() const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->rooms.size();
}

bool InMemoryChatRepository::Add(std::string_view roomId,
                                 std::string_view userId) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->members[std::string(roomId)].insert(std::string(userId)).second;
}

bool InMemoryChatRepository::Remove(std::string_view roomId,
                                    std::string_view userId) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  const auto members = impl_->members.find(std::string(roomId));
  return members != impl_->members.end() &&
         members->second.erase(std::string(userId)) != 0;
}

bool InMemoryChatRepository::Contains(std::string_view roomId,
                                      std::string_view userId) const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  const auto members = impl_->members.find(std::string(roomId));
  return members != impl_->members.end() &&
         members->second.find(std::string(userId)) != members->second.end();
}

std::vector<RoomMember> InMemoryChatRepository::List(
    std::string_view roomId) const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  std::vector<RoomMember> result;
  const auto members = impl_->members.find(std::string(roomId));
  if (members == impl_->members.end()) return result;
  result.reserve(members->second.size());
  for (const std::string& userId : members->second) {
    result.push_back(RoomMember{std::string(roomId), userId});
  }
  std::sort(result.begin(), result.end(),
            [](const RoomMember& left, const RoomMember& right) {
              return left.userId < right.userId;
            });
  return result;
}

std::size_t InMemoryChatRepository::Count(std::string_view roomId) const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  const auto members = impl_->members.find(std::string(roomId));
  return members == impl_->members.end() ? 0 : members->second.size();
}

MessageAppendResult InMemoryChatRepository::AppendOrGet(
    std::string_view roomId, std::string_view senderId,
    std::string_view clientMessageId, std::string_view content) {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  const std::string senderKey(senderId);
  const std::string clientKey(clientMessageId);
  const auto sender = impl_->messagesByClientId.find(senderKey);
  if (sender != impl_->messagesByClientId.end()) {
    const auto existing = sender->second.find(clientKey);
    if (existing != sender->second.end()) {
      return {ChatServiceError::kNone, existing->second, false};
    }
  }

  Message message{std::to_string(impl_->nextMessageId++), std::string(roomId),
                  std::string(senderId), std::string(clientMessageId),
                  std::string(content), UtcTimestamp()};
  impl_->messages[message.roomId].push_back(message);
  impl_->messagesByClientId[senderKey].emplace(clientKey, message);
  return {ChatServiceError::kNone, std::move(message), true};
}

std::vector<Message> InMemoryChatRepository::ListBefore(
    std::string_view roomId, std::uint64_t beforeId,
    std::size_t limit) const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  std::vector<Message> result;
  const auto messages = impl_->messages.find(std::string(roomId));
  if (messages == impl_->messages.end() || limit == 0) return result;

  for (auto message = messages->second.rbegin();
       message != messages->second.rend() && result.size() < limit; ++message) {
    const std::uint64_t messageId = std::stoull(message->id);
    if (messageId < beforeId) result.push_back(*message);
  }
  std::reverse(result.begin(), result.end());
  return result;
}

std::vector<Message> InMemoryChatRepository::ListAfter(
    std::string_view roomId, std::uint64_t afterId, std::size_t limit) const {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  std::vector<Message> result;
  const auto messages = impl_->messages.find(std::string(roomId));
  if (messages == impl_->messages.end() || limit == 0) return result;

  for (const Message& message : messages->second) {
    if (std::stoull(message.id) > afterId) {
      result.push_back(message);
      if (result.size() == limit) break;
    }
  }
  return result;
}

void InMemoryChatRepository::Reset() {
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->rooms.clear();
  impl_->members.clear();
  impl_->messages.clear();
  impl_->messagesByClientId.clear();
  impl_->nextRoomId = 10000000;
  impl_->nextMessageId = 20000000;
}

}  // namespace httpserver
