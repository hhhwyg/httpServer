#include "config/ServerConfig.h"

#include <charconv>
#include <cstdlib>
#include <string_view>

namespace {

std::string readEnvironment(const char* name) {
  const char* value = std::getenv(name);
  return value == nullptr ? std::string{} : std::string(value);
}

int readIntEnvironment(const char* name, int defaultValue, int minimum,
                       int maximum, std::vector<std::string>* errors) {
  const std::string value = readEnvironment(name);
  if (value.empty()) {
    return defaultValue;
  }

  int parsed = 0;
  const char* begin = value.data();
  const char* end = begin + value.size();
  const auto result = std::from_chars(begin, end, parsed);
  if (result.ec != std::errc{} || result.ptr != end || parsed < minimum ||
      parsed > maximum) {
    errors->emplace_back(std::string(name) + " must be an integer between " +
                         std::to_string(minimum) + " and " +
                         std::to_string(maximum));
    return defaultValue;
  }
  return parsed;
}

}  // namespace

bool DatabaseConfig::anyCredentialConfigured() const {
  return !user.empty() || !password.empty() || !database.empty();
}

bool DatabaseConfig::enabled() const {
  return !user.empty() && !password.empty() && !database.empty();
}

ServerConfig ServerConfig::LoadFromEnvironment() {
  ServerConfig config;
  config.jwt.secret = readEnvironment("HTTPSERVER_JWT_SECRET");
  const std::string issuer = readEnvironment("HTTPSERVER_JWT_ISSUER");
  if (!issuer.empty()) {
    config.jwt.issuer = issuer;
  }
  config.jwt.ttlSeconds = readIntEnvironment(
      "HTTPSERVER_JWT_TTL_SECONDS", static_cast<int>(config.jwt.ttlSeconds),
      60, 86400, &config.validationErrors);

  config.database.host = readEnvironment("HTTPSERVER_DB_HOST");
  if (config.database.host.empty()) {
    config.database.host = "127.0.0.1";
  }
  config.database.port = readIntEnvironment("HTTPSERVER_DB_PORT", 3306, 1,
                                             65535, &config.validationErrors);
  config.database.user = readEnvironment("HTTPSERVER_DB_USER");
  config.database.password = readEnvironment("HTTPSERVER_DB_PASSWORD");
  config.database.database = readEnvironment("HTTPSERVER_DB_NAME");
  config.database.poolSize = readIntEnvironment("HTTPSERVER_DB_POOL_SIZE", 4,
                                                 1, 64,
                                                 &config.validationErrors);

  if (!config.jwt.secret.empty() && config.jwt.secret.size() < 32) {
    config.validationErrors.emplace_back(
        "HTTPSERVER_JWT_SECRET must contain at least 32 bytes");
  }
  if (config.jwt.issuer.empty()) {
    config.validationErrors.emplace_back("HTTPSERVER_JWT_ISSUER must not be empty");
  }
  if (config.database.anyCredentialConfigured() && !config.database.enabled()) {
    config.validationErrors.emplace_back(
        "HTTPSERVER_DB_USER, HTTPSERVER_DB_PASSWORD and HTTPSERVER_DB_NAME "
        "must be configured together");
  }
  return config;
}
