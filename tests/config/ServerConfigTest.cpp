#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>

#include "httpserver/config/ServerConfig.h"

namespace {

void clearEnvironment() {
  unsetenv("HTTPSERVER_JWT_SECRET");
  unsetenv("HTTPSERVER_CONFIG_FILE");
  unsetenv("HTTPSERVER_JWT_ISSUER");
  unsetenv("HTTPSERVER_JWT_TTL_SECONDS");
  unsetenv("HTTPSERVER_DB_HOST");
  unsetenv("HTTPSERVER_DB_PORT");
  unsetenv("HTTPSERVER_DB_USER");
  unsetenv("HTTPSERVER_DB_PASSWORD");
  unsetenv("HTTPSERVER_DB_NAME");
  unsetenv("HTTPSERVER_DB_POOL_SIZE");
  unsetenv("HTTPSERVER_AUTH_MAX_ATTEMPTS");
  unsetenv("HTTPSERVER_AUTH_WINDOW_SECONDS");
  unsetenv("HTTPSERVER_MAX_FDS");
  unsetenv("HTTPSERVER_IO_URING_QUEUE_SIZE");
}

void testDefaultsDisableSensitiveFeatures() {
  clearEnvironment();
  const httpserver::ServerConfig config =
      httpserver::ServerConfig::LoadFromEnvironment();
  assert(config.valid());
  assert(!config.jwt.enabled());
  assert(!config.database.enabled());
}

void testInvalidConfigurationIsRejected() {
  clearEnvironment();
  setenv("HTTPSERVER_JWT_SECRET", "too-short", 1);
  setenv("HTTPSERVER_DB_USER", "httpserver", 1);
  const httpserver::ServerConfig config =
      httpserver::ServerConfig::LoadFromEnvironment();
  assert(!config.valid());
}

void testCompleteConfigurationIsAccepted() {
  clearEnvironment();
  setenv("HTTPSERVER_JWT_SECRET",
         "0123456789abcdef0123456789abcdef0123456789abcdef", 1);
  setenv("HTTPSERVER_JWT_ISSUER", "httpserver-test", 1);
  setenv("HTTPSERVER_DB_USER", "httpserver", 1);
  setenv("HTTPSERVER_DB_PASSWORD", "database-password", 1);
  setenv("HTTPSERVER_DB_NAME", "webserver", 1);
  setenv("HTTPSERVER_DB_POOL_SIZE", "2", 1);
  setenv("HTTPSERVER_AUTH_MAX_ATTEMPTS", "5", 1);
  setenv("HTTPSERVER_AUTH_WINDOW_SECONDS", "120", 1);
  setenv("HTTPSERVER_MAX_FDS", "20000", 1);
  setenv("HTTPSERVER_IO_URING_QUEUE_SIZE", "1024", 1);
  const httpserver::ServerConfig config =
      httpserver::ServerConfig::LoadFromEnvironment();
  assert(config.valid());
  assert(config.jwt.enabled());
  assert(config.database.enabled());
  assert(config.database.poolSize == 2);
  assert(config.authenticationRateLimit.maxAttempts == 5);
  assert(config.authenticationRateLimit.windowSeconds == 120);
  assert(config.limits.maxFds == 20000);
  assert(config.limits.ioUringQueueSize == 1024);
  clearEnvironment();
}

void testConfigFileAndEnvironmentOverride() {
  clearEnvironment();
  constexpr char kConfigPath[] = "/tmp/httpserver-server-config-test.toml";
  {
    std::ofstream config(kConfigPath);
    config << "[jwt]\n"
           << "secret = \"0123456789abcdef0123456789abcdef\"\n"
           << "issuer = \"from-file\"\n"
           << "ttl_seconds = 600\n"
           << "[database]\n"
           << "user = \"httpserver\"\n"
           << "password = \"database-password\"\n"
           << "name = \"webserver\"\n"
           << "[authentication_rate_limit]\n"
           << "max_attempts = 3\n"
           << "window_seconds = 90\n";
  }
  setenv("HTTPSERVER_CONFIG_FILE", kConfigPath, 1);
  setenv("HTTPSERVER_JWT_ISSUER", "from-environment", 1);
  const httpserver::ServerConfig config =
      httpserver::ServerConfig::LoadFromEnvironment();
  assert(config.valid());
  assert(config.jwt.issuer == "from-environment");
  assert(config.jwt.ttlSeconds == 600);
  assert(config.database.enabled());
  assert(config.authenticationRateLimit.maxAttempts == 3);
  assert(config.authenticationRateLimit.windowSeconds == 90);
  std::remove(kConfigPath);
  clearEnvironment();
}

}  // namespace

int main() {
  testDefaultsDisableSensitiveFeatures();
  testInvalidConfigurationIsRejected();
  testCompleteConfigurationIsAccepted();
  testConfigFileAndEnvironmentOverride();
  return 0;
}
