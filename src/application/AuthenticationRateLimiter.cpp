#include "httpserver/application/AuthenticationRateLimiter.h"

#include <chrono>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>

#include "httpserver/application/RedisClient.h"

namespace httpserver {
namespace {

using Clock = std::chrono::steady_clock;

struct RateLimitState {
  int maxAttempts = 10;
  int windowSeconds = 60;
  std::mutex mutex;
  std::unordered_map<std::string, std::deque<Clock::time_point>> attempts;
};

RateLimitState& state() {
  static RateLimitState instance;
  return instance;
}

void discardExpired(std::deque<Clock::time_point>* attempts, Clock::time_point cutoff) {
  while (!attempts->empty() && attempts->front() <= cutoff) {
    attempts->pop_front();
  }
}

bool allowLocked(RateLimitState* rateLimit, std::string_view identity, int maxAttempts,
                 int windowSeconds) {
  const Clock::time_point now = Clock::now();
  const Clock::time_point cutoff = now - std::chrono::seconds(windowSeconds);
  auto& attempts = rateLimit->attempts[std::string(identity)];
  discardExpired(&attempts, cutoff);
  if (static_cast<int>(attempts.size()) >= maxAttempts) return false;
  attempts.push_back(now);
  return true;
}

}  // namespace

void AuthenticationRateLimiter::Configure(int maxAttempts, int windowSeconds) {
  RateLimitState& rateLimit = state();
  std::lock_guard<std::mutex> lock(rateLimit.mutex);
  rateLimit.maxAttempts = maxAttempts;
  rateLimit.windowSeconds = windowSeconds;
  rateLimit.attempts.clear();
}

bool AuthenticationRateLimiter::Allow(std::string_view identity) {
  if (identity.empty()) {
    return false;
  }

  RateLimitState& rateLimit = state();
  std::lock_guard<std::mutex> lock(rateLimit.mutex);
  return allowLocked(&rateLimit, identity, rateLimit.maxAttempts, rateLimit.windowSeconds);
}

bool AuthenticationRateLimiter::Allow(std::string_view identity, int maxAttempts,
                                      int windowSeconds) {
  if (identity.empty() || maxAttempts <= 0 || windowSeconds <= 0) return false;
  RateLimitState& rateLimit = state();
  std::lock_guard<std::mutex> lock(rateLimit.mutex);
  return allowLocked(&rateLimit, identity, maxAttempts, windowSeconds);
}

bool AuthenticationRateLimiter::AllowDistributed(std::string_view scope, std::string_view identity,
                                                 int maxAttemptsOverride,
                                                 int windowSecondsOverride) {
  if (scope.empty() || identity.empty()) return false;
  RateLimitState& rateLimit = state();
  int maxAttempts = 0;
  int windowSeconds = 0;
  {
    std::lock_guard<std::mutex> lock(rateLimit.mutex);
    maxAttempts = rateLimit.maxAttempts;
    windowSeconds = rateLimit.windowSeconds;
  }
  if (maxAttemptsOverride > 0) maxAttempts = maxAttemptsOverride;
  if (windowSecondsOverride > 0) windowSeconds = windowSecondsOverride;
  const RedisRateLimitResult redis =
      RedisService::AllowRateLimit(scope, identity, maxAttempts, windowSeconds);
  if (redis == RedisRateLimitResult::kAllowed) return true;
  if (redis == RedisRateLimitResult::kLimited) return false;
  if (maxAttemptsOverride == 0 && windowSecondsOverride == 0) {
    return Allow(std::string(scope) + ':' + std::string(identity));
  }
  RateLimitState& fallback = state();
  const Clock::time_point now = Clock::now();
  std::lock_guard<std::mutex> lock(fallback.mutex);
  const Clock::time_point cutoff = now - std::chrono::seconds(windowSeconds);
  auto& attempts = fallback.attempts[std::string(scope) + ':' + std::string(identity)];
  discardExpired(&attempts, cutoff);
  if (static_cast<int>(attempts.size()) >= maxAttempts) return false;
  attempts.push_back(now);
  return true;
}

void AuthenticationRateLimiter::ResetForTesting() {
  RateLimitState& rateLimit = state();
  std::lock_guard<std::mutex> lock(rateLimit.mutex);
  rateLimit.maxAttempts = 10;
  rateLimit.windowSeconds = 60;
  rateLimit.attempts.clear();
}

}  // namespace httpserver
