#include "RoomController.h"

#include <charconv>
#include <exception>
#include <optional>

#include "httpserver/application/ChatService.h"
#include "httpserver/protocol/HttpRequest.h"
#include "json.hpp"

using json = nlohmann::json;

namespace httpserver::RoomController {
namespace {

std::optional<std::string> ParseRoomName(const ApplicationRequest& request) {
  try {
    const json body = json::parse(request.body);
    if (!body.is_object() || !body.contains("name") ||
        !body["name"].is_string()) {
      return std::nullopt;
    }
    return body["name"].get<std::string>();
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

json RoomJson(const RoomSummary& room) {
  return {{"id", room.id},
          {"name", room.name},
          {"ownerId", room.ownerId},
          {"memberCount", room.memberCount},
          {"joined", room.joined}};
}

json MessageJson(const Message& message) {
  return {{"messageId", message.id},
          {"roomId", message.roomId},
          {"senderId", message.senderId},
          {"clientMessageId", message.clientMessageId},
          {"content", message.content},
          {"createdAt", message.createdAt}};
}

std::optional<std::size_t> ParseHistoryLimit(std::string_view query) {
  const std::string value = HttpRequestParser::QueryParam(query, "limit");
  if (value.empty()) return 50;
  std::size_t limit = 0;
  const auto result =
      std::from_chars(value.data(), value.data() + value.size(), limit);
  if (result.ec != std::errc{} || result.ptr != value.data() + value.size() ||
      limit == 0 || limit > 100) {
    return std::nullopt;
  }
  return limit;
}

void SendV1Error(const ResponseSender& sendResponse, int status,
                 ChatServiceError error, std::string_view message) {
  sendResponse(status, "application/json",
               json{{"ok", false},
                    {"error", {{"code", ChatServiceErrorCode(error)},
                               {"message", message}}}}
                   .dump());
}

int ErrorStatus(ChatServiceError error) {
  switch (error) {
    case ChatServiceError::kInvalidArgument:
      return 400;
    case ChatServiceError::kRoomNotFound:
      return 404;
    case ChatServiceError::kNotMember:
      return 403;
    case ChatServiceError::kOwnerCannotLeave:
    case ChatServiceError::kAlreadyMember:
    case ChatServiceError::kDuplicateMessage:
      return 409;
    case ChatServiceError::kInvalidCursor:
      return 400;
    case ChatServiceError::kNone:
      return 200;
  }
  return 500;
}

}  // namespace

void handleCreateRoom(const ApplicationRequest& request,
                      std::string_view userId,
                      const ResponseSender& sendResponse) {
  const auto roomName = ParseRoomName(request);
  if (!roomName.has_value()) {
    sendResponse(400, "application/json",
                 "{\"ok\":false,\"msg\":\"Invalid room name\"}");
    return;
  }
  const CreateRoomResult result =
      ChatService::Instance().CreateRoom(userId, *roomName);
  if (result.error != ChatServiceError::kNone) {
    sendResponse(400, "application/json",
                 "{\"ok\":false,\"msg\":\"Invalid room name\"}");
    return;
  }
  sendResponse(200, "application/json",
               json{{"ok", true}, {"roomId", result.room.id}}.dump());
}

void handleGetRoomList(std::string_view userId,
                       const ResponseSender& sendResponse) {
  json response = {{"ok", true}, {"rooms", json::array()}};
  for (const RoomSummary& room : ChatService::Instance().ListRooms(userId)) {
    response["rooms"].push_back(
        {{"id", room.id}, {"name", room.name}});
  }
  sendResponse(200, "application/json", response.dump());
}

void handleCreateRoomV1(const ApplicationRequest& request,
                        std::string_view userId,
                        const ResponseSender& sendResponse) {
  const auto roomName = ParseRoomName(request);
  if (!roomName.has_value()) {
    SendV1Error(sendResponse, 400, ChatServiceError::kInvalidArgument,
                "room name must be a string");
    return;
  }
  const CreateRoomResult result =
      ChatService::Instance().CreateRoom(userId, *roomName);
  if (result.error != ChatServiceError::kNone) {
    SendV1Error(sendResponse, ErrorStatus(result.error), result.error,
                "room name must contain 1 to 128 non-whitespace bytes");
    return;
  }
  sendResponse(201, "application/json",
               json{{"ok", true}, {"data", {{"room", RoomJson(result.room)}}}}
                   .dump());
}

void handleGetRoomListV1(std::string_view userId,
                         const ResponseSender& sendResponse) {
  json rooms = json::array();
  for (const RoomSummary& room : ChatService::Instance().ListRooms(userId)) {
    rooms.push_back(RoomJson(room));
  }
  sendResponse(200, "application/json",
               json{{"ok", true}, {"data", {{"rooms", rooms}}}}.dump());
}

void handleJoinRoomV1(std::string_view roomId, std::string_view userId,
                      const ResponseSender& sendResponse) {
  const ChatServiceError error =
      ChatService::Instance().JoinRoom(userId, roomId);
  if (error == ChatServiceError::kAlreadyMember) {
    sendResponse(200, "application/json",
                 json{{"ok", true},
                      {"data", {{"roomId", roomId}, {"joined", false}}}}
                     .dump());
    return;
  }
  if (error != ChatServiceError::kNone) {
    SendV1Error(sendResponse, ErrorStatus(error), error,
                error == ChatServiceError::kRoomNotFound
                    ? "room does not exist"
                    : "unable to join room");
    return;
  }
  sendResponse(200, "application/json",
               json{{"ok", true},
                    {"data", {{"roomId", roomId}, {"joined", true}}}}
                   .dump());
}

void handleLeaveRoomV1(std::string_view roomId, std::string_view userId,
                       const ResponseSender& sendResponse) {
  const ChatServiceError error =
      ChatService::Instance().LeaveRoom(userId, roomId);
  if (error != ChatServiceError::kNone) {
    SendV1Error(sendResponse, ErrorStatus(error), error,
                error == ChatServiceError::kOwnerCannotLeave
                    ? "room owner cannot leave the room"
                    : "unable to leave room");
    return;
  }
  sendResponse(200, "application/json",
               json{{"ok", true}, {"data", {{"roomId", roomId}}}}.dump());
}

void handleGetRoomMembersV1(std::string_view roomId,
                            std::string_view userId,
                            const ResponseSender& sendResponse) {
  const RoomMembersResult result =
      ChatService::Instance().ListMembers(userId, roomId);
  if (result.error != ChatServiceError::kNone) {
    SendV1Error(sendResponse, ErrorStatus(result.error), result.error,
                result.error == ChatServiceError::kRoomNotFound
                    ? "room does not exist"
                    : "room membership is required");
    return;
  }
  sendResponse(200, "application/json",
               json{{"ok", true},
                    {"data", {{"roomId", roomId},
                              {"members", result.members}}}}
                   .dump());
}

void handleGetRoomMessagesV1(const ApplicationRequest& request,
                             std::string_view roomId,
                             std::string_view userId,
                             const ResponseSender& sendResponse) {
  const auto limit = ParseHistoryLimit(request.query);
  if (!limit.has_value()) {
    SendV1Error(sendResponse, 400, ChatServiceError::kInvalidArgument,
                "limit must be an integer between 1 and 100");
    return;
  }
  const std::string before =
      HttpRequestParser::QueryParam(request.query, "before");
  const MessageHistoryResult result =
      ChatService::Instance().ListMessages(userId, roomId, before, *limit);
  if (result.error != ChatServiceError::kNone) {
    SendV1Error(sendResponse, ErrorStatus(result.error), result.error,
                result.error == ChatServiceError::kInvalidCursor
                    ? "before must be a positive server message ID"
                    : result.error == ChatServiceError::kRoomNotFound
                          ? "room does not exist"
                          : "room membership is required");
    return;
  }

  json messages = json::array();
  for (const Message& message : result.messages) {
    messages.push_back(MessageJson(message));
  }
  json nextBefore = result.nextBefore.has_value()
                        ? json(*result.nextBefore)
                        : json(nullptr);
  sendResponse(200, "application/json",
               json{{"ok", true},
                    {"data", {{"messages", messages},
                              {"nextBefore", nextBefore}}}}
                   .dump());
}

}  // namespace httpserver::RoomController
