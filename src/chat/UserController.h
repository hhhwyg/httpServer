#pragma once

#include "httpserver/application/UserController.h"

namespace UserController {
using httpserver::UserController::checkLogin;
using httpserver::UserController::handleLogin;
using httpserver::UserController::handleRegister;
using httpserver::UserController::registerUser;
}
