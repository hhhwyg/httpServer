#include <cassert>
#include <string>
#include <string_view>

#include "httpserver/application/ApplicationRouter.h"
#include "httpserver/application/ChatApplication.h"

namespace {

void testUnknownRouteFallsThrough() {
  const auto result = httpserver::ApplicationRouter::Dispatch(
      nullptr, "GET", "/not-a-route", std::string_view{},
      httpserver::HeaderMap{});
  assert(result == httpserver::RouteResult::kNotHandled);
}

void testInvalidChatMessageProducesStableError() {
  std::string response;
  httpserver::ChatApplication::HandleMessage(
      "{", 42, nullptr,
      [&response](const std::string& message) { response = message; });
  assert(response.find("\"code\":\"invalid_message\"") !=
         std::string::npos);
  assert(response.find("\"type\":\"error\"") != std::string::npos);
}

}  // namespace

int main() {
  testUnknownRouteFallsThrough();
  testInvalidChatMessageProducesStableError();
  return 0;
}
