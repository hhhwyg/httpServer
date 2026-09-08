#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace httpserver {

struct JwtConfig {
  std::string secret;
  std::string issuer = "httpServer";
  std::int64_t ttlSeconds = 3600;

  bool enabled() const { return secret.size() >= 32; }
};

struct DatabaseConfig {
  std::string host = "127.0.0.1";
  int port = 3306;
  std::string user;
  std::string password;
  std::string database;
  int poolSize = 4;
  int operationTimeoutSeconds = 3;
  int executorWorkers = 2;
  int executorQueueCapacity = 256;

  bool anyCredentialConfigured() const;
  bool enabled() const;
};

struct RedisConfig {
  // An empty host disables Redis and keeps the local fallback active.
  std::string host;
  int port = 6379;
  std::string password;
  int connectTimeoutMilliseconds = 500;
  int commandTimeoutMilliseconds = 500;
  int queueCapacity = 256;
  int maxReconnectAttempts = 5;
  std::string instanceId;

  bool enabled() const { return !host.empty(); }
};

struct ServerLimits {
  int maxFds = 100000;
  unsigned ioUringQueueSize = 4096;
  int outputBufferHighWaterBytes = 256 * 1024;
  int outputBufferHardLimitBytes = 1024 * 1024;
  int gracefulShutdownSeconds = 5;
};

struct AuthenticationRateLimitConfig {
  int maxAttempts = 10;
  int windowSeconds = 60;
};

struct MessageRateLimitConfig {
  int maxMessages = 1000;
  int windowSeconds = 60;
};

struct WebSocketSecurityConfig {
  // An empty list is intended only for local development. Production startup
  // rejects it so browser WebSocket upgrades always have an explicit origin
  // boundary.
  std::vector<std::string> allowedOrigins;
  bool requireOrigin = false;
};

struct OutboxConfig {
  int batchSize = 32;
  int pollIntervalMilliseconds = 200;
  int leaseSeconds = 15;
  int maxBackoffSeconds = 60;
  std::string publisherId;
};

class ServerConfig {
 public:
  // HTTPSERVER_CONFIG_FILE is loaded first when set; individual environment
  // variables take precedence over the file values.
  static ServerConfig LoadFromEnvironment();

  bool valid() const { return validationErrors.empty(); }
  const std::vector<std::string>& errors() const { return validationErrors; }

  JwtConfig jwt;
  DatabaseConfig database;
  RedisConfig redis;
  OutboxConfig outbox;
  AuthenticationRateLimitConfig authenticationRateLimit;
  MessageRateLimitConfig messageRateLimit;
  WebSocketSecurityConfig webSocketSecurity;
  ServerLimits limits;

 private:
  std::vector<std::string> validationErrors;
};

}  // namespace httpserver
