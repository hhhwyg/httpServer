#pragma once

#include <string>

#include "httpserver/application/ApplicationContext.h"

namespace httpserver::UserController {

void handleRegister(const ApplicationRequest& request,
                    const ResponseSender& sendResponse);
void handleLogin(const ApplicationRequest& request,
                 const ResponseSender& sendResponse);
bool registerUser(const std::string& username, const std::string& password);
bool checkLogin(const std::string& username, const std::string& password);

}  // namespace httpserver::UserController
