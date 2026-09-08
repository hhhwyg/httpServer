#pragma once

#include <string_view>

namespace httpserver {

class AuthenticationRateLimiter {
 public:
  static void Configure(int maxAttempts, int windowSeconds);
  static bool Allow(std::string_view identity);
  // Must only be called from a worker thread. Redis failure deliberately
  // falls back to the local limiter so authentication remains protected.
  static bool AllowDistributed(std::string_view scope,
                               std::string_view identity,
                               int maxAttempts = 0,
                               int windowSeconds = 0);

  // Test-only state reset; production configuration always clears old attempts.
  static void ResetForTesting();
};

}  // namespace httpserver
