#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "httpserver/application/Authentication.h"

namespace httpserver {

struct ApplicationRequest {
  std::string method;
  std::string path;
  std::string query;
  HeaderMap headers;
  std::string body;
  // Direct peer address captured by the transport. Do not derive this from
  // untrusted forwarding headers.
  std::string remoteAddress;
};

using ResponseSender =
    std::function<void(int, std::string_view, std::string_view)>;

class ConnectionSession {
 public:
  virtual ~ConnectionSession() = default;

  virtual int fd() const = 0;
  virtual void sendMessage(std::string_view message) = 0;
};

}  // namespace httpserver
