#include "httpserver/application/UserRepository.h"

#include <array>

#include "SqlConnPool.h"

namespace httpserver {
namespace {

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

void bindString(MYSQL_BIND* binding, std::string_view value,
                unsigned long* length) {
  *length = static_cast<unsigned long>(value.size());
  binding->buffer_type = MYSQL_TYPE_STRING;
  binding->buffer = const_cast<char*>(value.data());
  binding->buffer_length = *length;
  binding->length = length;
}

class MysqlUserRepository final : public UserRepository {
 public:
  bool available() const override { return SqlConnPool::Instance()->IsInitialized(); }

  bool create(std::string_view username, std::string_view passwordHash) override {
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

  std::optional<std::string> passwordHashFor(std::string_view username) override {
    MYSQL* sql = nullptr;
    SqlConnRAII connection(&sql, SqlConnPool::Instance());
    if (!sql) {
      return std::nullopt;
    }

    MysqlStatement statement(sql);
    constexpr char kSelect[] =
        "SELECT passwd FROM `user` WHERE username = ? LIMIT 1";
    if (!statement.get() || mysql_stmt_prepare(statement.get(), kSelect,
                                                sizeof(kSelect) - 1) != 0) {
      return std::nullopt;
    }

    MYSQL_BIND parameter{};
    unsigned long usernameLength = 0;
    bindString(&parameter, username, &usernameLength);
    if (mysql_stmt_bind_param(statement.get(), &parameter) != 0 ||
        mysql_stmt_execute(statement.get()) != 0 ||
        mysql_stmt_store_result(statement.get()) != 0) {
      return std::nullopt;
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
      return std::nullopt;
    }
    return std::string(passwordHash.data(), passwordHashLength);
  }
};

}  // namespace

UserRepository& GetUserRepository() {
  static MysqlUserRepository repository;
  return repository;
}

}  // namespace httpserver
