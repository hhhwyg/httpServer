#pragma once

#include <string>

#include "httpserver/application/ApplicationContext.h"
#include "httpserver/application/UserRepository.h"

namespace httpserver::UserController {

void handleRegister(const ApplicationRequest& request,
                    const ResponseSender& sendResponse);
void handleLogin(const ApplicationRequest& request,
                 const ResponseSender& sendResponse);
void handleLogout(const AuthenticatedToken& token,
                  const ResponseSender& sendResponse);
UserCreateResult registerUser(const std::string& username,
                              const std::string& password);
bool checkLogin(const std::string& username, const std::string& password);

}  // namespace httpserver::UserController
