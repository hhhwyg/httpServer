#include "UserController.h"

#include <array>
#include <cctype>
#include <optional>
#include <utility>

#include "CryptoUtil.h"
#include "SqlConnPool.h"
#include "json.hpp"

using json = nlohmann::json;

namespace httpserver::UserController {
namespace {

constexpr std::size_t kUsernameMinLength = 3;
constexpr std::size_t kUsernameMaxLength = 64;
constexpr std::size_t kPasswordMinLength = 8;
constexpr std::size_t kPasswordMaxLength = 128;

class MysqlStatement {
 public:
  explicit MysqlStatement(MYSQL* connection) : statement_(mysql_stmt_init(connection)) {}
  ~MysqlStatement() {
    if (statement_) {
      mysql_stmt_close(statement_);
    }
  }

  MYSQL_STMT* get() const { return statement_; }

 private:
  MYSQL_STMT* statement_;
};

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

void bindString(MYSQL_BIND* binding, const std::string& value,
                unsigned long* length) {
  *length = static_cast<unsigned long>(value.size());
  binding->buffer_type = MYSQL_TYPE_STRING;
  binding->buffer = const_cast<char*>(value.data());
  binding->buffer_length = *length;
  binding->length = length;
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

bool registerUser(const std::string& username, const std::string& password) {
  if (!isValidUsername(username) || !isValidPassword(password)) {
    return false;
  }

  const std::string passwordHash = CryptoUtil::hashPassword(password);
  if (passwordHash.empty()) {
    return false;
  }

  MYSQL* sql = nullptr;
  SqlConnRAII connection(&sql, SqlConnPool::Instance());
  if (!sql) {
    return false;
  }

  MysqlStatement statement(sql);
  constexpr char kInsert[] =
      "INSERT INTO `user` (username, passwd) VALUES (?, ?)";
  if (!statement.get() || mysql_stmt_prepare(statement.get(), kInsert,
                                              sizeof(kInsert) - 1) != 0) {
    return false;
  }

  MYSQL_BIND parameters[2]{};
  unsigned long usernameLength = 0;
  unsigned long passwordHashLength = 0;
  bindString(&parameters[0], username, &usernameLength);
  bindString(&parameters[1], passwordHash, &passwordHashLength);
  return mysql_stmt_bind_param(statement.get(), parameters) == 0 &&
         mysql_stmt_execute(statement.get()) == 0;
}

bool checkLogin(const std::string& username, const std::string& password) {
  if (!isValidUsername(username) || !isValidPassword(password)) {
    return false;
  }

  MYSQL* sql = nullptr;
  SqlConnRAII connection(&sql, SqlConnPool::Instance());
  if (!sql) {
    return false;
  }

  MysqlStatement statement(sql);
  constexpr char kSelect[] =
      "SELECT passwd FROM `user` WHERE username = ? LIMIT 1";
  if (!statement.get() || mysql_stmt_prepare(statement.get(), kSelect,
                                              sizeof(kSelect) - 1) != 0) {
    return false;
  }

  MYSQL_BIND parameter{};
  unsigned long usernameLength = 0;
  bindString(&parameter, username, &usernameLength);
  if (mysql_stmt_bind_param(statement.get(), &parameter) != 0 ||
      mysql_stmt_execute(statement.get()) != 0 ||
      mysql_stmt_store_result(statement.get()) != 0) {
    return false;
  }

  std::array<char, 512> passwordHash{};
  unsigned long passwordHashLength = 0;
  bool isNull = false;
  MYSQL_BIND result{};
  result.buffer_type = MYSQL_TYPE_STRING;
  result.buffer = passwordHash.data();
  result.buffer_length = passwordHash.size();
  result.length = &passwordHashLength;
  result.is_null = &isNull;
  if (mysql_stmt_bind_result(statement.get(), &result) != 0 ||
      mysql_stmt_fetch(statement.get()) != 0 || isNull ||
      passwordHashLength >= passwordHash.size()) {
    return false;
  }
  return CryptoUtil::verifyPassword(
      password, std::string(passwordHash.data(), passwordHashLength));
}

void handleRegister(const ApplicationRequest& request,
                    const ResponseSender& sendResponse) {
  if (!SqlConnPool::Instance()->IsInitialized()) {
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
  if (!registerUser(credentials->first, credentials->second)) {
    // Do not distinguish duplicate users from database failures in the API.
    sendResponse(400, "application/json",
                 "{\"ok\":false,\"msg\":\"Registration failed\"}");
    return;
  }

  sendResponse(201, "application/json", "{\"ok\":true}");
}

void handleLogin(const ApplicationRequest& request,
                 const ResponseSender& sendResponse) {
  if (!SqlConnPool::Instance()->IsInitialized()) {
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
  if (!credentials.has_value() || !checkLogin(credentials->first, credentials->second)) {
    sendResponse(401, "application/json",
                 "{\"ok\":false,\"msg\":\"Invalid username or password\"}");
    return;
  }

  const std::string token = CryptoUtil::generateJWT(credentials->first);
  if (token.empty()) {
    sendResponse(503, "application/json",
                 "{\"ok\":false,\"msg\":\"Authentication unavailable\"}");
    return;
  }
  const json response = {{"ok", true}, {"token", token}};
  sendResponse(200, "application/json", response.dump());
}

}  // namespace httpserver::UserController
