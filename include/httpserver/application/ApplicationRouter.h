#pragma once

#include <string_view>

#include "httpserver/application/ApplicationContext.h"

namespace httpserver {

enum class RouteResult {
  kNotHandled,
  kHandled,
};

class ApplicationRouter {
 public:
  static RouteResult Dispatch(const ApplicationRequest& request,
                              const ResponseSender& sendResponse);
};

}  // namespace httpserver
