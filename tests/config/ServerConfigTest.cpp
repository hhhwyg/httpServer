#include <cassert>
#include <cstdlib>

#include "config/ServerConfig.h"

namespace {

void clearEnvironment() {
  unsetenv("HTTPSERVER_JWT_SECRET");
  unsetenv("HTTPSERVER_JWT_ISSUER");
  unsetenv("HTTPSERVER_JWT_TTL_SECONDS");
  unsetenv("HTTPSERVER_DB_HOST");
  unsetenv("HTTPSERVER_DB_PORT");
  unsetenv("HTTPSERVER_DB_USER");
  unsetenv("HTTPSERVER_DB_PASSWORD");
  unsetenv("HTTPSERVER_DB_NAME");
  unsetenv("HTTPSERVER_DB_POOL_SIZE");
}

void testDefaultsDisableSensitiveFeatures() {
  clearEnvironment();
  const ServerConfig config = ServerConfig::LoadFromEnvironment();
  assert(config.valid());
  assert(!config.jwt.enabled());
  assert(!config.database.enabled());
}

void testInvalidConfigurationIsRejected() {
  clearEnvironment();
  setenv("HTTPSERVER_JWT_SECRET", "too-short", 1);
  setenv("HTTPSERVER_DB_USER", "httpserver", 1);
  const ServerConfig config = ServerConfig::LoadFromEnvironment();
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
  const ServerConfig config = ServerConfig::LoadFromEnvironment();
  assert(config.valid());
  assert(config.jwt.enabled());
  assert(config.database.enabled());
  assert(config.database.poolSize == 2);
  clearEnvironment();
}

}  // namespace

int main() {
  testDefaultsDisableSensitiveFeatures();
  testInvalidConfigurationIsRejected();
  testCompleteConfigurationIsAccepted();
  return 0;
}
