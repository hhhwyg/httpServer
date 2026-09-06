#include "httpserver/application/ChatApplication.h"

#include <atomic>
#include <exception>

#include "httpserver/application/ChatService.h"
#include "httpserver/protocol/ChatProtocol.h"
#include "json.hpp"

namespace {

using json = nlohmann::json;

std::atomic<std::uint64_t> legacyMessageSequence{1};

void SendLegacyError(
    const httpserver::ChatApplication::SendMessage& sendMessage,
    const char* code) {
  if (sendMessage) {
    sendMessage(json{{"type", "error"}, {"code", code}}.dump());
  }
}

const char* ErrorMessage(httpserver::ChatServiceError error) {
  switch (error) {
    case httpserver::ChatServiceError::kInvalidArgument:
      return "message fields are invalid";
    case httpserver::ChatServiceError::kRoomNotFound:
      return "room does not exist";
    case httpserver::ChatServiceError::kNotMember:
      return "room membership and subscription are required";
    case httpserver::ChatServiceError::kDuplicateMessage:
      return "clientMessageId has already been used";
    case httpserver::ChatServiceError::kAlreadyMember:
    case httpserver::ChatServiceError::kOwnerCannotLeave:
    case httpserver::ChatServiceError::kInvalidCursor:
    case httpserver::ChatServiceError::kNone:
      return "request could not be completed";
  }
  return "request could not be completed";
}

void HandleV1(
    const httpserver::ChatCommand& command,
    const std::shared_ptr<httpserver::ConnectionSession>& session,
    const httpserver::ChatApplication::SendMessage& sendMessage) {
  httpserver::ChatService& service = httpserver::ChatService::Instance();
  if (command.type == httpserver::ChatCommandType::kSubscribe) {
    const httpserver::ChatServiceError error =
        service.AttachSession(command.roomId, session);
    if (error != httpserver::ChatServiceError::kNone) {
      sendMessage(httpserver::ChatProtocol::Error(
          command.requestId, httpserver::ChatServiceErrorCode(error),
          ErrorMessage(error)));
      return;
    }
    sendMessage(httpserver::ChatProtocol::Subscribed(command.requestId,
                                                      command.roomId));
    return;
  }

  httpserver::MessageDispatchResult result = service.DispatchMessage(
      command.roomId, session->userId(), command.clientMessageId,
      command.content, session->sessionId());
  if (result.error != httpserver::ChatServiceError::kNone) {
    sendMessage(httpserver::ChatProtocol::Error(
        command.requestId, httpserver::ChatServiceErrorCode(result.error),
        ErrorMessage(result.error)));
    return;
  }

  sendMessage(httpserver::ChatProtocol::MessageAck(command.requestId,
                                                    result.message));
  const std::string push =
      httpserver::ChatProtocol::MessageCreated(result.message);
  for (const auto& recipient : result.recipients) {
    recipient->sendMessage(push);
  }
}

void HandleLegacy(
    std::string_view payload,
    const std::shared_ptr<httpserver::ConnectionSession>& session,
    const httpserver::ChatApplication::SendMessage& sendMessage) {
  try {
    const json message = json::parse(payload);
    if (!message.is_object()) {
      SendLegacyError(sendMessage, "invalid_message");
      return;
    }
    const std::string type = message.value("type", "");
    if (type == "join" && message.contains("roomId") &&
        message["roomId"].is_string()) {
      const httpserver::ChatServiceError error =
          httpserver::ChatService::Instance().AttachSession(
              message["roomId"].get<std::string>(), session, true);
      if (error != httpserver::ChatServiceError::kNone) {
        SendLegacyError(sendMessage, httpserver::ChatServiceErrorCode(error));
      }
      return;
    }

    if (type == "chat" && message.contains("roomId") &&
        message.contains("content") && message["roomId"].is_string() &&
        message["content"].is_string()) {
      const std::string roomId = message["roomId"].get<std::string>();
      const std::string content = message["content"].get<std::string>();
      const std::string clientMessageId =
          "legacy-" + std::to_string(session->sessionId()) + '-' +
          std::to_string(
              legacyMessageSequence.fetch_add(1, std::memory_order_relaxed));
      const httpserver::MessageDispatchResult result =
          httpserver::ChatService::Instance().DispatchMessage(
              roomId, session->userId(), clientMessageId, content,
              session->sessionId());
      if (result.error != httpserver::ChatServiceError::kNone) {
        SendLegacyError(sendMessage,
                        httpserver::ChatServiceErrorCode(result.error));
        return;
      }
      const std::string broadcast =
          json{{"type", "chat"},
               {"roomId", roomId},
               {"content", content},
               {"user", std::string(session->userId())}}
              .dump();
      for (const auto& recipient : result.recipients) {
        recipient->sendMessage(broadcast);
      }
      return;
    }
  } catch (const std::exception&) {
  }
  SendLegacyError(sendMessage, "invalid_message");
}

}  // namespace

namespace httpserver {

void ChatApplication::HandleMessage(
    std::string_view payload,
    const std::shared_ptr<ConnectionSession>& session,
    const SendMessage& sendMessage) {
  if (!session || session->userId().empty() || !sendMessage) {
    SendLegacyError(sendMessage, "invalid_message");
    return;
  }

  const ChatProtocolParseResult parsed = ChatProtocol::Parse(payload);
  if (parsed.command.has_value()) {
    HandleV1(*parsed.command, session, sendMessage);
    return;
  }
  if (parsed.versioned) {
    sendMessage(ChatProtocol::Error(parsed.requestId, parsed.errorCode,
                                    parsed.errorMessage));
    return;
  }
  HandleLegacy(payload, session, sendMessage);
}

void ChatApplication::Leave(std::uint64_t sessionId) {
  ChatService::Instance().DetachSession(sessionId);
}

}  // namespace httpserver
