#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "httpserver/application/ApplicationContext.h"
#include "httpserver/domain/ChatDomain.h"
#include "httpserver/domain/ChatRepositories.h"

namespace httpserver {

struct SendMessageCommand {
  std::string roomId;
  std::string senderId;
  std::string clientMessageId;
  std::string content;
  std::uint64_t senderSessionId = 0;
};

struct SendMessageResult {
  ChatServiceError error = ChatServiceError::kNone;
  Message message;
  MessageDeliveryStage stage = MessageDeliveryStage::kReceived;
  // false means this is an idempotent retry of an already persisted message.
  bool inserted = false;
  std::vector<std::shared_ptr<ConnectionSession>> recipients;
};

using MessageDispatchResult = SendMessageResult;

class ChatService {
 public:
  static ChatService& Instance();

  void ConfigureRepository(std::unique_ptr<ChatRepository> repository);
  void ConfigureMessageRateLimit(int maxMessages, int windowSeconds);
  bool AllowMessage(std::string_view userId) const;
  bool persistenceAvailable() const;
  bool outboxEnabled() const;

  CreateRoomResult CreateRoom(std::string_view ownerId,
                              std::string_view name);
  std::vector<RoomSummary> ListRooms(std::string_view userId) const;
  ChatServiceError JoinRoom(std::string_view userId,
                            std::string_view roomId);
  ChatServiceError LeaveRoom(std::string_view userId,
                             std::string_view roomId);
  RoomMembersResult ListMembers(std::string_view userId,
                                std::string_view roomId) const;
  SendMessageResult SendMessage(const SendMessageCommand& command);
  MessageDispatchResult DispatchMessage(
      std::string_view roomId, std::string_view senderId,
      std::string_view clientMessageId, std::string_view content,
      std::uint64_t senderSessionId);
  MessageHistoryResult ListMessages(std::string_view userId,
                                    std::string_view roomId,
                                    std::string_view before,
                                    std::size_t limit) const;
  MessageHistoryResult ListMessagesAfter(std::string_view userId,
                                         std::string_view roomId,
                                         std::string_view after,
                                         std::size_t limit) const;

  ChatServiceError AttachSession(
      std::string_view roomId,
      const std::shared_ptr<ConnectionSession>& session,
      bool enrollIfMissing = false);
  ChatServiceError Broadcast(std::string_view roomId,
                             std::string_view message,
                             std::uint64_t senderSessionId);
  // Called by the Redis subscriber thread. ConnectionSession::sendMessage
  // queues delivery to the owning EventLoop, so no transport state is touched
  // while this method holds the session registry lock.
  bool DeliverOutboxEvent(std::string_view event);
  void DetachSession(std::uint64_t sessionId);

  std::size_t RoomCount() const;
  std::size_t CachedRoomCount() const;
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
