#include "httpserver/application/ChatApplication.h"

#include <atomic>
#include <chrono>
#include <exception>

#include "httpserver/application/ChatService.h"
#include "httpserver/application/AuthenticationRateLimiter.h"
#include "httpserver/application/DatabaseExecutor.h"
#include "httpserver/application/Metrics.h"
#include "httpserver/application/StructuredLog.h"
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
    case httpserver::ChatServiceError::kDependencyUnavailable:
      return "message persistence is temporarily unavailable";
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
      httpserver::Metrics::WebSocketMessage("subscribe", false);
      httpserver::LogStructuredEvent({command.requestId,
          std::to_string(session->sessionId()), {}, {}, "websocket.subscribe",
          httpserver::ChatServiceErrorCode(error), 0});
      sendMessage(httpserver::ChatProtocol::Error(
          command.requestId, httpserver::ChatServiceErrorCode(error),
          ErrorMessage(error)));
      return;
    }
    httpserver::Metrics::WebSocketMessage("subscribe", true);
    httpserver::LogStructuredEvent({command.requestId,
        std::to_string(session->sessionId()), {}, {}, "websocket.subscribe", {}, 0});
    sendMessage(httpserver::ChatProtocol::Subscribed(command.requestId,
                                                      command.roomId));
    return;
  }

  if (!service.AllowMessage(session->userId())) {
    sendMessage(httpserver::ChatProtocol::Error(
        command.requestId, "rate_limited", "too many messages"));
    httpserver::Metrics::WebSocketMessage("message", false);
    return;
  }

  httpserver::MessageDispatchResult result = service.DispatchMessage(
      command.roomId, session->userId(), command.clientMessageId,
      command.content, session->sessionId());
  if (result.error != httpserver::ChatServiceError::kNone) {
    httpserver::Metrics::WebSocketMessage("message", false);
    sendMessage(httpserver::ChatProtocol::Error(
        command.requestId, httpserver::ChatServiceErrorCode(result.error),
        ErrorMessage(result.error)));
    return;
  }

  sendMessage(httpserver::ChatProtocol::MessageAck(command.requestId,
                                                    result.message));
  httpserver::Metrics::WebSocketMessage("message", true);
  httpserver::LogStructuredEvent({command.requestId,
      std::to_string(session->sessionId()), {}, result.message.id,
      "websocket.message", {}, 0});
  // A retry is acknowledged from the persisted record but never republished.
  if (!result.inserted) return;

  // Realtime fan-out is performed only after the durable Outbox publisher
  // emits this message. The ACK remains the persistence guarantee.
  if (!service.outboxEnabled()) {
    const std::string push = httpserver::ChatProtocol::MessageCreated(result.message);
    for (const auto& recipient : result.recipients) recipient->sendMessage(push);
  }
}

bool SubmitChatWork(const httpserver::ChatApplication::SendMessage& sendMessage,
                    std::function<void()> work, std::string_view requestId,
                    bool versioned) {
  if (httpserver::DatabaseExecutor::Submit(
          [sendMessage, work = std::move(work)] {
            try {
              work();
            } catch (const std::exception&) {
              SendLegacyError(sendMessage, "dependency_unavailable");
            }
          }, std::chrono::seconds(3), [sendMessage, requestId = std::string(requestId), versioned] {
            if (versioned) {
              sendMessage(httpserver::ChatProtocol::Error(
                  requestId, "dependency_unavailable", "database operation deadline exceeded"));
            } else {
              SendLegacyError(sendMessage, "dependency_unavailable");
            }
          })) {
    return true;
  }
  if (versioned) {
    sendMessage(httpserver::ChatProtocol::Error(
        requestId, "dependency_unavailable", "database operation queue is full"));
  } else {
    SendLegacyError(sendMessage, "dependency_unavailable");
  }
  return false;
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
      if (!httpserver::ChatService::Instance().AllowMessage(session->userId())) {
        SendLegacyError(sendMessage, "rate_limited");
        return;
      }
      const httpserver::MessageDispatchResult result =
          httpserver::ChatService::Instance().DispatchMessage(
              roomId, session->userId(), clientMessageId, content,
              session->sessionId());
      if (result.error != httpserver::ChatServiceError::kNone) {
        SendLegacyError(sendMessage,
                        httpserver::ChatServiceErrorCode(result.error));
        return;
      }
      if (!httpserver::ChatService::Instance().outboxEnabled()) {
        const std::string broadcast = json{{"type", "chat"},
            {"roomId", roomId}, {"content", content},
            {"user", std::string(session->userId())}}.dump();
        for (const auto& recipient : result.recipients) recipient->sendMessage(broadcast);
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
    Metrics::WebSocketMessage("invalid", false);
    SendLegacyError(sendMessage, "invalid_message");
    return;
  }

  const ChatProtocolParseResult parsed = ChatProtocol::Parse(payload);
  if (parsed.command.has_value()) {
    const ChatCommand command = *parsed.command;
    SubmitChatWork(sendMessage,
                   [command, session, sendMessage] {
                     HandleV1(command, session, sendMessage);
                   },
                   command.requestId, true);
    return;
  }
  if (parsed.versioned) {
    Metrics::WebSocketMessage("invalid", false);
    sendMessage(ChatProtocol::Error(parsed.requestId, parsed.errorCode,
                                    parsed.errorMessage));
    return;
  }
  const std::string legacyPayload(payload);
  SubmitChatWork(sendMessage,
                 [legacyPayload, session, sendMessage] {
                   HandleLegacy(legacyPayload, session, sendMessage);
                 },
                 {}, false);
}

void ChatApplication::Leave(std::uint64_t sessionId) {
  ChatService::Instance().DetachSession(sessionId);
}

}  // namespace httpserver
