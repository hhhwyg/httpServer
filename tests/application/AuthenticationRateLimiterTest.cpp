#include <cassert>

#include "httpserver/application/AuthenticationRateLimiter.h"

int main() {
  httpserver::AuthenticationRateLimiter::Configure(2, 60);
  assert(httpserver::AuthenticationRateLimiter::Allow("login:user:alice"));
  assert(httpserver::AuthenticationRateLimiter::Allow("login:user:alice"));
  assert(!httpserver::AuthenticationRateLimiter::Allow("login:user:alice"));
  assert(httpserver::AuthenticationRateLimiter::Allow("login:user:bob"));
  assert(httpserver::AuthenticationRateLimiter::Allow("login:ip:127.0.0.1"));
  assert(httpserver::AuthenticationRateLimiter::Allow("websocket:user:alice"));
  assert(!httpserver::AuthenticationRateLimiter::Allow(""));
  httpserver::AuthenticationRateLimiter::ResetForTesting();
  return 0;
}
