#pragma once

#include <memory>
#include <string>

class HttpData;

namespace httpserver::UserController {

void handleRegister(std::shared_ptr<::HttpData> httpData);
void handleLogin(std::shared_ptr<::HttpData> httpData);
bool registerUser(const std::string& username, const std::string& password);
bool checkLogin(const std::string& username, const std::string& password);

}  // namespace httpserver::UserController
