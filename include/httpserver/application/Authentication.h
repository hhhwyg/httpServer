#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace httpserver {

using HeaderMap = std::unordered_map<std::string, std::string>;

class Authenticator {
 public:
  static std::optional<std::string> ExtractToken(
      std::string_view query, const HeaderMap& headers);
  static std::optional<std::string> VerifyQuery(std::string_view query);
  static std::optional<std::string> VerifyRequest(
      std::string_view query, const HeaderMap& headers);
};

}  // namespace httpserver
