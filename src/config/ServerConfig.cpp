#include "httpserver/config/ServerConfig.h"

#include <charconv>
#include <cstdlib>
#include <fstream>
#include <unordered_map>
#include <string_view>

namespace {

using Settings = std::unordered_map<std::string, std::string>;

std::string trim(std::string value) {
  const std::size_t begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) return {};
  const std::size_t end = value.find_last_not_of(" \t\r\n");
  return value.substr(begin, end - begin + 1);
}

Settings loadConfigFile(const std::string& path, std::vector<std::string>* errors) {
  std::ifstream file(path);
  if (!file) {
    errors->emplace_back("HTTPSERVER_CONFIG_FILE could not be opened");
    return {};
  }

  Settings values;
  std::string section;
  std::string line;
  int lineNumber = 0;
  while (std::getline(file, line)) {
    ++lineNumber;
    const std::size_t comment = line.find('#');
    line = trim(line.substr(0, comment));
    if (line.empty()) continue;
    if (line.front() == '[' && line.back() == ']') {
      section = trim(line.substr(1, line.size() - 2));
      if (section.empty()) {
        errors->emplace_back("Invalid empty TOML section at line " +
                             std::to_string(lineNumber));
      }
      continue;
    }
    const std::size_t equals = line.find('=');
    if (equals == std::string::npos || section.empty()) {
      errors->emplace_back("Invalid TOML setting at line " +
                           std::to_string(lineNumber));
      continue;
    }
    std::string value = trim(line.substr(equals + 1));
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
      value = value.substr(1, value.size() - 2);
    }
    values[section + "." + trim(line.substr(0, equals))] = value;
  }
  return values;
}

std::string readSetting(const char* environmentName, std::string_view configName,
                        const Settings& values) {
  const char* value = std::getenv(environmentName);
  if (value != nullptr && *value != '\0') return value;
  const auto setting = values.find(std::string(configName));
  return setting == values.end() ? std::string{} : setting->second;
}

int readIntSetting(const char* name, std::string_view configName,
                   const Settings& values, int defaultValue, int minimum,
                   int maximum, std::vector<std::string>* errors) {
  const std::string value = readSetting(name, configName, values);
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

namespace httpserver {

bool DatabaseConfig::anyCredentialConfigured() const {
  return !user.empty() || !password.empty() || !database.empty();
}

bool DatabaseConfig::enabled() const {
  return !user.empty() && !password.empty() && !database.empty();
}

ServerConfig ServerConfig::LoadFromEnvironment() {
  ServerConfig config;
  Settings fileSettings;
  if (const char* configPath = std::getenv("HTTPSERVER_CONFIG_FILE");
      configPath != nullptr && *configPath != '\0') {
    fileSettings = loadConfigFile(configPath, &config.validationErrors);
  }

  config.jwt.secret = readSetting("HTTPSERVER_JWT_SECRET", "jwt.secret", fileSettings);
  const std::string issuer =
      readSetting("HTTPSERVER_JWT_ISSUER", "jwt.issuer", fileSettings);
  if (!issuer.empty()) {
    config.jwt.issuer = issuer;
  }
  config.jwt.ttlSeconds = readIntSetting(
      "HTTPSERVER_JWT_TTL_SECONDS", "jwt.ttl_seconds", fileSettings,
      static_cast<int>(config.jwt.ttlSeconds), 60, 86400, &config.validationErrors);

  config.database.host = readSetting("HTTPSERVER_DB_HOST", "database.host", fileSettings);
  if (config.database.host.empty()) {
    config.database.host = "127.0.0.1";
  }
  config.database.port = readIntSetting("HTTPSERVER_DB_PORT", "database.port",
                                         fileSettings, 3306, 1, 65535,
                                         &config.validationErrors);
  config.database.user = readSetting("HTTPSERVER_DB_USER", "database.user", fileSettings);
  config.database.password = readSetting("HTTPSERVER_DB_PASSWORD", "database.password", fileSettings);
  config.database.database = readSetting("HTTPSERVER_DB_NAME", "database.name", fileSettings);
  config.database.poolSize = readIntSetting("HTTPSERVER_DB_POOL_SIZE", "database.pool_size",
                                             fileSettings, 4, 1, 64,
                                             &config.validationErrors);
  config.authenticationRateLimit.maxAttempts = readIntSetting(
      "HTTPSERVER_AUTH_MAX_ATTEMPTS", "authentication_rate_limit.max_attempts", fileSettings, 10, 1, 1000,
      &config.validationErrors);
  config.authenticationRateLimit.windowSeconds = readIntSetting(
      "HTTPSERVER_AUTH_WINDOW_SECONDS", "authentication_rate_limit.window_seconds", fileSettings, 60, 1, 3600,
      &config.validationErrors);
  config.limits.maxFds = readIntSetting("HTTPSERVER_MAX_FDS", "limits.max_fds", fileSettings, 100000,
                                            1024, 1000000,
                                            &config.validationErrors);
  config.limits.ioUringQueueSize = static_cast<unsigned>(readIntSetting(
      "HTTPSERVER_IO_URING_QUEUE_SIZE", "limits.io_uring_queue_size", fileSettings, 4096, 64, 65536,
      &config.validationErrors));

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

}  // namespace httpserver
