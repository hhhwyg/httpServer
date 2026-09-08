#include "httpserver/config/ServerConfig.h"

#include <charconv>
#include <cstdlib>
#include <fstream>
#include <sstream>
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

std::vector<std::string> readCommaSeparatedSetting(
    const char* environmentName, std::string_view configName,
    const Settings& values) {
  std::vector<std::string> result;
  std::stringstream input(readSetting(environmentName, configName, values));
  std::string item;
  while (std::getline(input, item, ',')) {
    item = trim(std::move(item));
    if (!item.empty()) result.push_back(std::move(item));
  }
  return result;
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
  config.database.operationTimeoutSeconds = readIntSetting(
      "HTTPSERVER_DB_OPERATION_TIMEOUT_SECONDS", "database.operation_timeout_seconds",
      fileSettings, 3, 1, 60, &config.validationErrors);
  config.database.executorWorkers = readIntSetting(
      "HTTPSERVER_DB_EXECUTOR_WORKERS", "database.executor_workers",
      fileSettings, 2, 1, 64, &config.validationErrors);
  config.database.executorQueueCapacity = readIntSetting(
      "HTTPSERVER_DB_EXECUTOR_QUEUE_CAPACITY", "database.executor_queue_capacity",
      fileSettings, 256, 1, 65536, &config.validationErrors);
  config.redis.host = readSetting("HTTPSERVER_REDIS_HOST", "redis.host", fileSettings);
  config.redis.port = readIntSetting("HTTPSERVER_REDIS_PORT", "redis.port",
                                     fileSettings, 6379, 1, 65535,
                                     &config.validationErrors);
  config.redis.password = readSetting("HTTPSERVER_REDIS_PASSWORD", "redis.password", fileSettings);
  config.redis.connectTimeoutMilliseconds = readIntSetting(
      "HTTPSERVER_REDIS_CONNECT_TIMEOUT_MS", "redis.connect_timeout_ms",
      fileSettings, 500, 50, 30000, &config.validationErrors);
  config.redis.commandTimeoutMilliseconds = readIntSetting(
      "HTTPSERVER_REDIS_COMMAND_TIMEOUT_MS", "redis.command_timeout_ms",
      fileSettings, 500, 50, 30000, &config.validationErrors);
  config.redis.queueCapacity = readIntSetting(
      "HTTPSERVER_REDIS_QUEUE_CAPACITY", "redis.queue_capacity",
      fileSettings, 256, 1, 65536, &config.validationErrors);
  config.redis.maxReconnectAttempts = readIntSetting(
      "HTTPSERVER_REDIS_MAX_RECONNECT_ATTEMPTS", "redis.max_reconnect_attempts",
      fileSettings, 5, 1, 100, &config.validationErrors);
  config.redis.instanceId = readSetting("HTTPSERVER_INSTANCE_ID", "redis.instance_id", fileSettings);
  config.outbox.batchSize = readIntSetting("HTTPSERVER_OUTBOX_BATCH_SIZE",
      "outbox.batch_size", fileSettings, 32, 1, 256, &config.validationErrors);
  config.outbox.pollIntervalMilliseconds = readIntSetting(
      "HTTPSERVER_OUTBOX_POLL_INTERVAL_MS", "outbox.poll_interval_ms", fileSettings,
      200, 10, 10000, &config.validationErrors);
  config.outbox.leaseSeconds = readIntSetting("HTTPSERVER_OUTBOX_LEASE_SECONDS",
      "outbox.lease_seconds", fileSettings, 15, 1, 300, &config.validationErrors);
  config.outbox.maxBackoffSeconds = readIntSetting(
      "HTTPSERVER_OUTBOX_MAX_BACKOFF_SECONDS", "outbox.max_backoff_seconds",
      fileSettings, 60, 1, 3600, &config.validationErrors);
  config.outbox.publisherId = readSetting("HTTPSERVER_OUTBOX_PUBLISHER_ID",
      "outbox.publisher_id", fileSettings);
  config.authenticationRateLimit.maxAttempts = readIntSetting(
      "HTTPSERVER_AUTH_MAX_ATTEMPTS", "authentication_rate_limit.max_attempts", fileSettings, 10, 1, 1000,
      &config.validationErrors);
  config.authenticationRateLimit.windowSeconds = readIntSetting(
      "HTTPSERVER_AUTH_WINDOW_SECONDS", "authentication_rate_limit.window_seconds", fileSettings, 60, 1, 3600,
      &config.validationErrors);
  config.messageRateLimit.maxMessages = readIntSetting(
      "HTTPSERVER_MESSAGE_MAX_ATTEMPTS", "message_rate_limit.max_messages", fileSettings,
      1000, 1, 1000000, &config.validationErrors);
  config.messageRateLimit.windowSeconds = readIntSetting(
      "HTTPSERVER_MESSAGE_WINDOW_SECONDS", "message_rate_limit.window_seconds", fileSettings,
      60, 1, 3600, &config.validationErrors);
  config.webSocketSecurity.allowedOrigins = readCommaSeparatedSetting(
      "HTTPSERVER_WS_ALLOWED_ORIGINS", "websocket_security.allowed_origins",
      fileSettings);
  config.webSocketSecurity.requireOrigin =
      !config.webSocketSecurity.allowedOrigins.empty();
  config.limits.maxFds = readIntSetting("HTTPSERVER_MAX_FDS", "limits.max_fds", fileSettings, 100000,
                                            1024, 1000000,
                                            &config.validationErrors);
  config.limits.ioUringQueueSize = static_cast<unsigned>(readIntSetting(
      "HTTPSERVER_IO_URING_QUEUE_SIZE", "limits.io_uring_queue_size", fileSettings, 4096, 64, 65536,
      &config.validationErrors));
  config.limits.outputBufferHighWaterBytes = readIntSetting(
      "HTTPSERVER_OUTPUT_BUFFER_HIGH_WATER_BYTES", "limits.output_buffer_high_water_bytes",
      fileSettings, 256 * 1024, 4096, 64 * 1024 * 1024,
      &config.validationErrors);
  config.limits.outputBufferHardLimitBytes = readIntSetting(
      "HTTPSERVER_OUTPUT_BUFFER_HARD_LIMIT_BYTES", "limits.output_buffer_hard_limit_bytes",
      fileSettings, 1024 * 1024, 8192, 64 * 1024 * 1024,
      &config.validationErrors);
  config.limits.gracefulShutdownSeconds = readIntSetting(
      "HTTPSERVER_GRACEFUL_SHUTDOWN_SECONDS", "limits.graceful_shutdown_seconds",
      fileSettings, 5, 1, 120, &config.validationErrors);

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
  if (config.limits.outputBufferHardLimitBytes <=
      config.limits.outputBufferHighWaterBytes) {
    config.validationErrors.emplace_back(
        "HTTPSERVER_OUTPUT_BUFFER_HARD_LIMIT_BYTES must exceed "
        "HTTPSERVER_OUTPUT_BUFFER_HIGH_WATER_BYTES");
  }
  for (const std::string& origin : config.webSocketSecurity.allowedOrigins) {
    if (origin.rfind("https://", 0) != 0 || origin.find('/', 8) != std::string::npos ||
        origin.size() <= 8) {
      config.validationErrors.emplace_back(
          "HTTPSERVER_WS_ALLOWED_ORIGINS entries must be HTTPS origins without paths");
      break;
    }
  }
  return config;
}

}  // namespace httpserver
