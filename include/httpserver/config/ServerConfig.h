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

  bool anyCredentialConfigured() const;
  bool enabled() const;
};

struct ServerLimits {
  int maxFds = 100000;
  unsigned ioUringQueueSize = 4096;
};

struct AuthenticationRateLimitConfig {
  int maxAttempts = 10;
  int windowSeconds = 60;
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
  AuthenticationRateLimitConfig authenticationRateLimit;
  ServerLimits limits;

 private:
  std::vector<std::string> validationErrors;
};

}  // namespace httpserver
