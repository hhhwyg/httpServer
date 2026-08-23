#pragma once
#include <string>
#include <memory>
#include "../httpdata/HttpData.h"

namespace UserController {
    void handleRegister(std::shared_ptr<HttpData> httpData);
    void handleLogin(std::shared_ptr<HttpData> httpData);
    bool registerUser(const std::string& username, const std::string& password);
    bool checkLogin(const std::string& username, const std::string& password);
}
