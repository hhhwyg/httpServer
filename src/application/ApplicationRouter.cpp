#include "httpserver/application/ApplicationRouter.h"

#include "httpserver/application/RoomController.h"
#include "httpserver/application/UserController.h"
#include "httpdata/HttpData.h"

#include <utility>

namespace httpserver {

RouteResult ApplicationRouter::Dispatch(std::shared_ptr<::HttpData> request,
                                        std::string_view method,
                                        std::string_view path,
                                        std::string_view query,
                                        const HeaderMap& headers) {
  if (method == "POST" && path == "/register") {
    UserController::handleRegister(std::move(request));
    return RouteResult::kHandled;
  }

  if ((method == "GET" || method == "HEAD") && path == "/ping") {
    request->sendResponse(200, "text/plain", "OK");
    return RouteResult::kHandled;
  }

  if (method == "POST" && path == "/login") {
    UserController::handleLogin(std::move(request));
    return RouteResult::kHandled;
  }

  const bool requiresAuthentication =
      (method == "POST" && path == "/room/create") ||
      (method == "GET" && path == "/room/list");
  if (!requiresAuthentication) return RouteResult::kNotHandled;

  if (!Authenticator::VerifyRequest(query, headers).has_value()) {
    request->sendResponse(401, "application/json",
                          "{\"ok\":false,\"msg\":\"Unauthorized\"}");
    return RouteResult::kHandled;
  }

  if (path == "/room/create") {
    RoomController::handleCreateRoom(std::move(request));
  } else {
    RoomController::handleGetRoomList(std::move(request));
  }
  return RouteResult::kHandled;
}

}  // namespace httpserver
