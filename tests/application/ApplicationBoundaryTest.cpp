#include <cassert>
#include <string>
#include <string_view>

#include "httpserver/application/ApplicationRouter.h"
#include "httpserver/application/ChatApplication.h"

namespace {

void testUnknownRouteFallsThrough() {
  const httpserver::ApplicationRequest request{
      "GET", "/not-a-route", "", {}, "", "127.0.0.1", ""};
  const auto result =
      httpserver::ApplicationRouter::Dispatch(request, {});
  assert(result == httpserver::RouteResult::kNotHandled);
}

void testInvalidChatMessageProducesStableError() {
  std::string response;
  httpserver::ChatApplication::HandleMessage(
      "{", nullptr,
      [&response](const std::string& message) { response = message; });
  assert(response.find("\"code\":\"invalid_message\"") !=
         std::string::npos);
  assert(response.find("\"type\":\"error\"") != std::string::npos);
}

void testV1RoomRouteRequiresAuthentication() {
  const httpserver::ApplicationRequest request{
      "GET", "/api/v1/rooms", "", {}, "", "127.0.0.1", ""};
  int status = 0;
  std::string response;
  const auto result = httpserver::ApplicationRouter::Dispatch(
      request,
      [&status, &response](int value, std::string_view,
                           std::string_view body) {
        status = value;
        response = std::string(body);
      });
  assert(result == httpserver::RouteResult::kHandled);
  assert(status == 401);
  assert(response.find("\"code\":\"unauthorized\"") != std::string::npos);
}

}  // namespace

int main() {
  testUnknownRouteFallsThrough();
  testInvalidChatMessageProducesStableError();
  testV1RoomRouteRequiresAuthentication();
  return 0;
}
