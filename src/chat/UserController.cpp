#include "UserController.h"

#include <cctype>
#include <chrono>
#include <optional>
#include <utility>

#include "CryptoUtil.h"
#include "httpserver/application/AuthenticationRateLimiter.h"
#include "httpserver/application/DatabaseExecutor.h"
#include "httpserver/application/RedisClient.h"
#include "httpserver/application/UserRepository.h"
#include "json.hpp"

using json = nlohmann::json;

namespace httpserver::UserController {
namespace {

constexpr std::size_t kUsernameMinLength = 3;
constexpr std::size_t kUsernameMaxLength = 64;
constexpr std::size_t kPasswordMinLength = 8;
constexpr std::size_t kPasswordMaxLength = 128;

bool isValidUsername(const std::string& username) {
  if (username.size() < kUsernameMinLength || username.size() > kUsernameMaxLength) {
    return false;
  }
  for (const unsigned char character : username) {
    if (!std::isalnum(character) && character != '_' && character != '-') {
      return false;
    }
  }
  return true;
}

bool isValidPassword(const std::string& password) {
  return password.size() >= kPasswordMinLength &&
         password.size() <= kPasswordMaxLength;
}

std::optional<std::pair<std::string, std::string>> parseCredentials(
    const ApplicationRequest& request) {
  try {
    const json body = json::parse(request.body);
    if (!body.is_object() || !body.contains("username") ||
        !body.contains("password") || !body["username"].is_string() ||
        !body["password"].is_string()) {
      return std::nullopt;
    }
    return std::make_pair(body["username"].get<std::string>(),
                          body["password"].get<std::string>());
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

}  // namespace

UserCreateResult registerUser(const std::string& username,
                              const std::string& password) {
  if (!isValidUsername(username) || !isValidPassword(password)) {
    return UserCreateResult::kAlreadyExists;
  }

  const std::string passwordHash = CryptoUtil::hashPassword(password);
  if (passwordHash.empty()) {
    return UserCreateResult::kUnavailable;
  }

  return GetUserRepository().create(username, passwordHash);
}

bool checkLogin(const std::string& username, const std::string& password) {
  if (!isValidUsername(username) || !isValidPassword(password)) {
    return false;
  }

  const auto passwordHash = GetUserRepository().passwordHashFor(username);
  return passwordHash.has_value() &&
         CryptoUtil::verifyPassword(password, *passwordHash);
}

void handleRegister(const ApplicationRequest& request,
                    const ResponseSender& sendResponse) {
  if (!GetUserRepository().available()) {
    sendResponse(503, "application/json",
                 "{\"ok\":false,\"msg\":\"Registration unavailable\"}");
    return;
  }

  const auto credentials = parseCredentials(request);
  if (!credentials.has_value() || !isValidUsername(credentials->first) ||
      !isValidPassword(credentials->second)) {
    sendResponse(400, "application/json",
                 "{\"ok\":false,\"msg\":\"Invalid credentials\"}");
    return;
  }
  const std::string username = credentials->first;
  const std::string password = credentials->second;
  const std::string remoteAddress = request.remoteAddress;
  if (!DatabaseExecutor::Submit([username, password, remoteAddress, sendResponse] {
        const bool userAllowed = AuthenticationRateLimiter::AllowDistributed(
            "register:user", username);
        const bool ipAllowed = remoteAddress.empty() ||
            AuthenticationRateLimiter::AllowDistributed("register:ip", remoteAddress,
                                                        1000, 60);
        if (!userAllowed || !ipAllowed) {
          sendResponse(429, "application/json",
                       "{\"ok\":false,\"msg\":\"Too many attempts\"}");
          return;
        }
        const UserCreateResult result = registerUser(username, password);
        if (result == UserCreateResult::kUnavailable) {
          sendResponse(503, "application/json",
                       "{\"ok\":false,\"msg\":\"Registration unavailable\"}");
          return;
        }
        if (result != UserCreateResult::kCreated) {
          sendResponse(400, "application/json",
                       "{\"ok\":false,\"msg\":\"Registration failed\"}");
          return;
        }
        sendResponse(201, "application/json", "{\"ok\":true}");
      }, std::chrono::seconds(3), [sendResponse] {
        sendResponse(503, "application/json",
                     "{\"ok\":false,\"msg\":\"Registration unavailable\"}");
      })) {
    sendResponse(503, "application/json",
                 "{\"ok\":false,\"msg\":\"Registration unavailable\"}");
  }
}

void handleLogin(const ApplicationRequest& request,
                 const ResponseSender& sendResponse) {
  if (!GetUserRepository().available()) {
    sendResponse(503, "application/json",
                 "{\"ok\":false,\"msg\":\"Login unavailable\"}");
    return;
  }
  if (!CryptoUtil::jwtEnabled()) {
    sendResponse(503, "application/json",
                 "{\"ok\":false,\"msg\":\"Authentication unavailable\"}");
    return;
  }

  const auto credentials = parseCredentials(request);
  if (!credentials.has_value()) {
    sendResponse(401, "application/json",
                 "{\"ok\":false,\"msg\":\"Invalid username or password\"}");
    return;
  }
  const std::string username = credentials->first;
  const std::string password = credentials->second;
  const std::string remoteAddress = request.remoteAddress;
  if (!DatabaseExecutor::Submit([username, password, remoteAddress, sendResponse] {
        const bool userAllowed = AuthenticationRateLimiter::AllowDistributed(
            "login:user", username);
        const bool ipAllowed = remoteAddress.empty() ||
            AuthenticationRateLimiter::AllowDistributed("login:ip", remoteAddress,
                                                        1000, 60);
        if (!userAllowed || !ipAllowed) {
          sendResponse(429, "application/json",
                       "{\"ok\":false,\"msg\":\"Too many attempts\"}");
          return;
        }
        if (!checkLogin(username, password)) {
          sendResponse(401, "application/json",
                       "{\"ok\":false,\"msg\":\"Invalid username or password\"}");
          return;
        }
        const std::string token = CryptoUtil::generateJWT(username);
        if (token.empty()) {
          sendResponse(503, "application/json",
                       "{\"ok\":false,\"msg\":\"Authentication unavailable\"}");
          return;
        }
        const json response = {{"ok", true}, {"token", token}};
        sendResponse(200, "application/json", response.dump());
      }, std::chrono::seconds(3), [sendResponse] {
        sendResponse(503, "application/json",
                     "{\"ok\":false,\"msg\":\"Login unavailable\"}");
      })) {
    sendResponse(503, "application/json",
                 "{\"ok\":false,\"msg\":\"Login unavailable\"}");
  }
}

void handleLogout(const AuthenticatedToken& token,
                  const ResponseSender& sendResponse) {
  const std::int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
  const std::int64_t remaining = token.expiresAt - now;
  if (token.jti.empty() || remaining <= 0 || !RedisService::enabled()) {
    sendResponse(503, "application/json",
                 "{\"ok\":false,\"msg\":\"Logout unavailable\"}");
    return;
  }
  const int ttlSeconds = static_cast<int>(std::min<std::int64_t>(remaining, 86400));
  if (!DatabaseExecutor::Submit([jti = token.jti, ttlSeconds, sendResponse] {
        if (!RedisService::RevokeToken(jti, ttlSeconds)) {
          sendResponse(503, "application/json",
                       "{\"ok\":false,\"msg\":\"Logout unavailable\"}");
          return;
        }
        sendResponse(204, "application/json", "");
      }, std::chrono::seconds(3), [sendResponse] {
        sendResponse(503, "application/json",
                     "{\"ok\":false,\"msg\":\"Logout unavailable\"}");
      })) {
    sendResponse(503, "application/json",
                 "{\"ok\":false,\"msg\":\"Logout unavailable\"}");
  }
}

}  // namespace httpserver::UserController
