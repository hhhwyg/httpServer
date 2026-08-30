#include "httpserver/application/ApplicationRouter.h"

#include "httpserver/application/OperationalStatus.h"
#include "httpserver/application/RoomController.h"
#include "httpserver/application/UserController.h"

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

  const bool requiresAuthentication =
      (request.method == "POST" && request.path == "/room/create") ||
      (request.method == "GET" && request.path == "/room/list");
  if (!requiresAuthentication) return RouteResult::kNotHandled;

  if (!Authenticator::VerifyRequest(request.query, request.headers).has_value()) {
    sendResponse(401, "application/json",
                 "{\"ok\":false,\"msg\":\"Unauthorized\"}");
    return RouteResult::kHandled;
  }

  if (request.path == "/room/create") {
    RoomController::handleCreateRoom(request, sendResponse);
  } else {
    RoomController::handleGetRoomList(request, sendResponse);
  }
  return RouteResult::kHandled;
}

}  // namespace httpserver
