#include "httpserver/application/ApplicationRouter.h"

#include <optional>
#include <string>

#include "httpserver/application/OperationalStatus.h"
#include "httpserver/application/RoomController.h"
#include "httpserver/application/UserController.h"
#include "json.hpp"

namespace {

enum class RoomMemberRoute {
  kCollection,
  kCurrentUser,
  kMessages,
};

struct MatchedRoomRoute {
  std::string roomId;
  RoomMemberRoute route = RoomMemberRoute::kCollection;
};

std::optional<MatchedRoomRoute> MatchRoomMemberRoute(
    std::string_view path) {
  constexpr std::string_view prefix = "/api/v1/rooms/";
  constexpr std::string_view membersSuffix = "/members";
  constexpr std::string_view meSuffix = "/members/me";
  constexpr std::string_view messagesSuffix = "/messages";
  if (path.size() <= prefix.size() || path.substr(0, prefix.size()) != prefix) {
    return std::nullopt;
  }

  std::string_view remainder = path.substr(prefix.size());
  RoomMemberRoute route = RoomMemberRoute::kCollection;
  if (remainder.size() > meSuffix.size() &&
      remainder.substr(remainder.size() - meSuffix.size()) == meSuffix) {
    remainder.remove_suffix(meSuffix.size());
    route = RoomMemberRoute::kCurrentUser;
  } else if (remainder.size() > messagesSuffix.size() &&
             remainder.substr(remainder.size() - messagesSuffix.size()) ==
                 messagesSuffix) {
    remainder.remove_suffix(messagesSuffix.size());
    route = RoomMemberRoute::kMessages;
  } else if (remainder.size() > membersSuffix.size() &&
             remainder.substr(remainder.size() - membersSuffix.size()) ==
                 membersSuffix) {
    remainder.remove_suffix(membersSuffix.size());
  } else {
    return std::nullopt;
  }
  if (remainder.empty() || remainder.find('/') != std::string_view::npos) {
    return std::nullopt;
  }
  return MatchedRoomRoute{std::string(remainder), route};
}

bool IsProtectedRoomRoute(const httpserver::ApplicationRequest& request,
                          const std::optional<MatchedRoomRoute>& memberRoute) {
  if ((request.method == "POST" && request.path == "/room/create") ||
      (request.method == "GET" && request.path == "/room/list")) {
    return true;
  }
  if (request.path == "/api/v1/rooms") {
    return request.method == "GET" || request.method == "POST";
  }
  if (!memberRoute.has_value()) return false;
  if (memberRoute->route == RoomMemberRoute::kCurrentUser) {
    return request.method == "DELETE";
  }
  if (memberRoute->route == RoomMemberRoute::kMessages) {
    return request.method == "GET";
  }
  return request.method == "GET" || request.method == "POST";
}

void SendUnauthorized(const httpserver::ApplicationRequest& request,
                      const httpserver::ResponseSender& sendResponse) {
  if (request.path.rfind("/api/v1/", 0) == 0) {
    sendResponse(401, "application/json",
                 nlohmann::json{{"ok", false},
                                {"error", {{"code", "unauthorized"},
                                           {"message", "authentication required"}}}}
                     .dump());
    return;
  }
  sendResponse(401, "application/json",
               "{\"ok\":false,\"msg\":\"Unauthorized\"}");
}

}  // namespace

namespace httpserver {

RouteResult ApplicationRouter::Dispatch(const ApplicationRequest& request,
                                        const ResponseSender& sendResponse) {
  if (request.method == "POST" && request.path == "/register") {
    UserController::handleRegister(request, sendResponse);
    return RouteResult::kHandled;
  }

  if ((request.method == "GET" || request.method == "HEAD") &&
      request.path == "/ping") {
    sendResponse(200, "text/plain", "OK");
    return RouteResult::kHandled;
  }

  if ((request.method == "GET" || request.method == "HEAD") &&
      request.path == "/healthz") {
    sendResponse(200, "text/plain", "ok\n");
    return RouteResult::kHandled;
  }

  if ((request.method == "GET" || request.method == "HEAD") &&
      request.path == "/readyz") {
    const bool ready = OperationalStatus::ready();
    sendResponse(ready ? 200 : 503, "text/plain",
                 ready ? "ready\n" : "not ready\n");
    return RouteResult::kHandled;
  }

  if ((request.method == "GET" || request.method == "HEAD") &&
      request.path == "/metrics") {
    sendResponse(200, "text/plain; version=0.0.4",
                 OperationalStatus::prometheusMetrics());
    return RouteResult::kHandled;
  }

  if (request.method == "POST" && request.path == "/login") {
    UserController::handleLogin(request, sendResponse);
    return RouteResult::kHandled;
  }

  const auto memberRoute = MatchRoomMemberRoute(request.path);
  const bool requiresAuthentication =
      IsProtectedRoomRoute(request, memberRoute);
  if (!requiresAuthentication) return RouteResult::kNotHandled;

  const auto userId =
      Authenticator::VerifyRequest(request.query, request.headers);
  if (!userId.has_value()) {
    SendUnauthorized(request, sendResponse);
    return RouteResult::kHandled;
  }

  if (request.path == "/room/create") {
    RoomController::handleCreateRoom(request, *userId, sendResponse);
  } else if (request.path == "/room/list") {
    RoomController::handleGetRoomList(*userId, sendResponse);
  } else if (request.path == "/api/v1/rooms" && request.method == "POST") {
    RoomController::handleCreateRoomV1(request, *userId, sendResponse);
  } else if (request.path == "/api/v1/rooms" && request.method == "GET") {
    RoomController::handleGetRoomListV1(*userId, sendResponse);
  } else if (memberRoute.has_value() &&
             memberRoute->route == RoomMemberRoute::kCurrentUser) {
    RoomController::handleLeaveRoomV1(memberRoute->roomId, *userId,
                                      sendResponse);
  } else if (memberRoute.has_value() &&
             memberRoute->route == RoomMemberRoute::kMessages) {
    RoomController::handleGetRoomMessagesV1(request, memberRoute->roomId,
                                            *userId, sendResponse);
  } else if (memberRoute.has_value() && request.method == "POST") {
    RoomController::handleJoinRoomV1(memberRoute->roomId, *userId,
                                     sendResponse);
  } else if (memberRoute.has_value()) {
    RoomController::handleGetRoomMembersV1(memberRoute->roomId, *userId,
                                           sendResponse);
  } else {
    return RouteResult::kNotHandled;
  }
  return RouteResult::kHandled;
}

}  // namespace httpserver
