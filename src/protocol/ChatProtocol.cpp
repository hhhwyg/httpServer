#include "httpserver/protocol/ChatProtocol.h"

#include <exception>
#include <utility>

#include "json.hpp"

namespace httpserver {
namespace {

using json = nlohmann::json;

ChatProtocolParseResult Failure(bool versioned, std::string requestId,
                                std::string code, std::string message) {
  return {versioned, std::nullopt, std::move(requestId), std::move(code),
          std::move(message)};
}

bool ValidIdentifier(const json& value) {
  return value.is_string() && !value.get_ref<const std::string&>().empty() &&
         value.get_ref<const std::string&>().size() <=
             ChatProtocol::kMaxIdentifierBytes;
}

json MessageJson(const Message& message) {
  return {{"messageId", message.id},
          {"roomId", message.roomId},
          {"senderId", message.senderId},
          {"clientMessageId", message.clientMessageId},
          {"content", message.content},
          {"createdAt", message.createdAt}};
}

}  // namespace

ChatProtocolParseResult ChatProtocol::Parse(std::string_view payload) {
  json envelope;
  try {
    envelope = json::parse(payload);
  } catch (const std::exception&) {
    return Failure(false, {}, "invalid_json", "message must be valid JSON");
  }
  if (!envelope.is_object() || !envelope.contains("version")) {
    return Failure(false, {}, "legacy_message", "legacy message");
  }

  std::string requestId;
  if (envelope.contains("requestId") && envelope["requestId"].is_string()) {
    requestId = envelope["requestId"].get<std::string>();
  }
  int version = 0;
  try {
    if (!envelope["version"].is_number_integer()) {
      return Failure(true, requestId, "unsupported_version",
                     "only protocol version 1 is supported");
    }
    version = envelope["version"].get<int>();
  } catch (const std::exception&) {
    return Failure(true, requestId, "unsupported_version",
                   "only protocol version 1 is supported");
  }
  if (version != kVersion) {
    return Failure(true, requestId, "unsupported_version",
                   "only protocol version 1 is supported");
  }
  if (!envelope.contains("requestId") ||
      !ValidIdentifier(envelope["requestId"])) {
    return Failure(true, requestId, "invalid_request_id",
                   "requestId must contain 1 to 128 bytes");
  }
  if (!envelope.contains("type") || !envelope["type"].is_string() ||
      !envelope.contains("payload") || !envelope["payload"].is_object()) {
    return Failure(true, requestId, "invalid_envelope",
                   "type and payload are required");
  }

  const std::string type = envelope["type"].get<std::string>();
  const json& commandPayload = envelope["payload"];
  if (!commandPayload.contains("roomId") ||
      !ValidIdentifier(commandPayload["roomId"])) {
    return Failure(true, requestId, "invalid_room_id",
                   "roomId must contain 1 to 128 bytes");
  }

  ChatCommand command;
  command.requestId = requestId;
  command.roomId = commandPayload["roomId"].get<std::string>();
  if (type == "room.subscribe") {
    command.type = ChatCommandType::kSubscribe;
    return {true, command, requestId, {}, {}};
  }
  if (type != "message.send") {
    return Failure(true, requestId, "unknown_message_type",
                   "unsupported message type");
  }
  if (!commandPayload.contains("clientMessageId") ||
      !ValidIdentifier(commandPayload["clientMessageId"])) {
    return Failure(true, requestId, "invalid_client_message_id",
                   "clientMessageId must contain 1 to 128 bytes");
  }
  if (!commandPayload.contains("content") ||
      !commandPayload["content"].is_string()) {
    return Failure(true, requestId, "invalid_content",
                   "content must be a string");
  }
  const std::string content = commandPayload["content"].get<std::string>();
  if (content.empty() || content.size() > kMaxContentBytes) {
    return Failure(true, requestId, "invalid_content",
                   "content must contain 1 to 4096 bytes");
  }
  command.type = ChatCommandType::kSendMessage;
  command.clientMessageId =
      commandPayload["clientMessageId"].get<std::string>();
  command.content = content;
  return {true, command, requestId, {}, {}};
}

std::string ChatProtocol::Subscribed(std::string_view requestId,
                                     std::string_view roomId) {
  return json{{"version", kVersion},
              {"type", "room.subscribed"},
              {"requestId", requestId},
              {"payload", {{"roomId", roomId}}}}
      .dump();
}

std::string ChatProtocol::MessageAck(std::string_view requestId,
                                     const Message& message) {
  return json{{"version", kVersion},
              {"type", "message.ack"},
              {"requestId", requestId},
              {"payload", {{"message", MessageJson(message)}}}}
      .dump();
}

std::string ChatProtocol::MessageCreated(const Message& message) {
  return json{{"version", kVersion},
              {"type", "message.created"},
              {"payload", {{"message", MessageJson(message)}}}}
      .dump();
}

std::string ChatProtocol::Error(std::string_view requestId,
                                std::string_view code,
                                std::string_view message) {
  json envelope = {{"version", kVersion},
                   {"type", "error"},
                   {"error", {{"code", code}, {"message", message}}}};
  if (!requestId.empty()) envelope["requestId"] = requestId;
  return envelope.dump();
}

}  // namespace httpserver
