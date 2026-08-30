#pragma once

#include <string_view>

namespace httpserver {

class AuthenticationRateLimiter {
 public:
  static void Configure(int maxAttempts, int windowSeconds);
  static bool Allow(std::string_view identity);

  // Test-only state reset; production configuration always clears old attempts.
  static void ResetForTesting();
};

}  // namespace httpserver
