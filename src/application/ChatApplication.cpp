#include "httpserver/application/ChatApplication.h"

#include "ChatManager.h"
#include "json.hpp"

namespace {

using json = nlohmann::json;

void SendError(const httpserver::ChatApplication::SendMessage& sendMessage,
               const char* code) {
  if (sendMessage) {
    sendMessage(json{{"type", "error"}, {"code", code}}.dump());
  }
}

}  // namespace

namespace httpserver {

void ChatApplication::HandleMessage(
    std::string_view payload,
    int senderFd,
    const std::shared_ptr<::HttpData>& user,
    const SendMessage& sendMessage) {
  try {
    const json message = json::parse(payload);
    const std::string type = message.value("type", "");
    if (type == "join" && message.contains("roomId") &&
        message["roomId"].is_string()) {
      const std::string roomId = message["roomId"].get<std::string>();
      if (!ChatManager::getInstance().joinRoom(roomId, user)) {
        SendError(sendMessage, "room_not_found");
      }
      return;
    }

    if (type == "chat" && message.contains("roomId") &&
        message.contains("content") && message["roomId"].is_string() &&
        message["content"].is_string()) {
      const std::string roomId = message["roomId"].get<std::string>();
      const std::string content = message["content"].get<std::string>();
      const std::string broadcast =
          json{{"type", "chat"}, {"roomId", roomId},
               {"content", content}}
              .dump();
      if (content.size() > 4096 ||
          !ChatManager::getInstance().broadcastToRoom(roomId, broadcast,
                                                       senderFd)) {
        SendError(sendMessage, "room_membership_required");
      }
      return;
    }
  } catch (const std::exception&) {
    SendError(sendMessage, "invalid_message");
  }
}

void ChatApplication::Leave(int senderFd) {
  ChatManager::getInstance().leaveUser(senderFd);
}

}  // namespace httpserver
