#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace httpserver {

class UserRepository {
 public:
  virtual ~UserRepository() = default;

  virtual bool available() const = 0;
  virtual bool create(std::string_view username,
                      std::string_view passwordHash) = 0;
  virtual std::optional<std::string> passwordHashFor(
      std::string_view username) = 0;
};

UserRepository& GetUserRepository();

}  // namespace httpserver
