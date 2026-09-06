#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "httpserver/application/ApplicationContext.h"
#include "httpserver/domain/ChatDomain.h"

namespace httpserver {

struct MessageDispatchResult {
  ChatServiceError error = ChatServiceError::kNone;
  Message message;
  std::vector<std::shared_ptr<ConnectionSession>> recipients;
};

class ChatService {
 public:
  static ChatService& Instance();

  CreateRoomResult CreateRoom(std::string_view ownerId,
                              std::string_view name);
  std::vector<RoomSummary> ListRooms(std::string_view userId) const;
  ChatServiceError JoinRoom(std::string_view userId,
                            std::string_view roomId);
  ChatServiceError LeaveRoom(std::string_view userId,
                             std::string_view roomId);
  RoomMembersResult ListMembers(std::string_view userId,
                                std::string_view roomId) const;
  MessageDispatchResult DispatchMessage(
      std::string_view roomId, std::string_view senderId,
      std::string_view clientMessageId, std::string_view content,
      std::uint64_t senderSessionId);
  MessageHistoryResult ListMessages(std::string_view userId,
                                    std::string_view roomId,
                                    std::string_view before,
                                    std::size_t limit) const;

  ChatServiceError AttachSession(
      std::string_view roomId,
      const std::shared_ptr<ConnectionSession>& session,
      bool enrollIfMissing = false);
  ChatServiceError Broadcast(std::string_view roomId,
                             std::string_view message,
                             std::uint64_t senderSessionId);
  void DetachSession(std::uint64_t sessionId);

  std::size_t RoomCount() const;
  void ResetForTesting();

 private:
  ChatService();
  ~ChatService();
  ChatService(const ChatService&) = delete;
  ChatService& operator=(const ChatService&) = delete;

  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace httpserver
