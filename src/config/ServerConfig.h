#pragma once

#include <cstdint>
#include <string>
#include <vector>

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

// Process configuration is read once at startup. Secrets are intentionally
// supplied only through environment variables and are never logged.
class ServerConfig {
 public:
  static ServerConfig LoadFromEnvironment();

  bool valid() const { return validationErrors.empty(); }
  const std::vector<std::string>& errors() const { return validationErrors; }

  JwtConfig jwt;
  DatabaseConfig database;

 private:
  std::vector<std::string> validationErrors;
};
