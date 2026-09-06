#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace httpserver {

struct User {
  std::string id;
};

struct Room {
  std::string id;
  std::string name;
  std::string ownerId;
};

struct RoomMember {
  std::string roomId;
  std::string userId;
};

struct Message {
  std::string id;
  std::string roomId;
  std::string senderId;
  std::string clientMessageId;
  std::string content;
  std::string createdAt;
};

// The durable acceptance boundary is kPersisted. Later stages describe
// best-effort realtime delivery and are deliberately not client guarantees.
enum class MessageDeliveryStage {
  kReceived,
  kPersisted,
  kPublished,
  kDelivered,
};

struct MessageAppendResult {
  Message message;
  bool inserted = false;
};

struct RoomSummary {
  std::string id;
  std::string name;
  std::string ownerId;
  std::size_t memberCount = 0;
  bool joined = false;
};

enum class ChatServiceError {
  kNone,
  kInvalidArgument,
  kRoomNotFound,
  kAlreadyMember,
  kNotMember,
  kOwnerCannotLeave,
  kInvalidCursor,
};

struct CreateRoomResult {
  ChatServiceError error = ChatServiceError::kNone;
  RoomSummary room;
};

struct RoomMembersResult {
  ChatServiceError error = ChatServiceError::kNone;
  std::vector<std::string> members;
};

struct MessageHistoryResult {
  ChatServiceError error = ChatServiceError::kNone;
  std::vector<Message> messages;
  std::optional<std::string> nextBefore;
  std::optional<std::string> nextAfter;
};

const char* ChatServiceErrorCode(ChatServiceError error);

}  // namespace httpserver
