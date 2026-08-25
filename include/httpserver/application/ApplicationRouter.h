#pragma once

#include <memory>
#include <string_view>

#include "httpserver/application/Authentication.h"

class HttpData;

namespace httpserver {

enum class RouteResult {
  kNotHandled,
  kHandled,
};

class ApplicationRouter {
 public:
  static RouteResult Dispatch(std::shared_ptr<::HttpData> request,
                              std::string_view method,
                              std::string_view path,
                              std::string_view query,
                              const HeaderMap& headers);
};

}  // namespace httpserver
