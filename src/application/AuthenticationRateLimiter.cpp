#include "httpserver/application/AuthenticationRateLimiter.h"

#include <chrono>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>

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

void discardExpired(std::deque<Clock::time_point>* attempts,
                    Clock::time_point cutoff) {
  while (!attempts->empty() && attempts->front() <= cutoff) {
    attempts->pop_front();
  }
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
  const Clock::time_point now = Clock::now();
  std::lock_guard<std::mutex> lock(rateLimit.mutex);
  const Clock::time_point cutoff =
      now - std::chrono::seconds(rateLimit.windowSeconds);
  auto& attempts = rateLimit.attempts[std::string(identity)];
  discardExpired(&attempts, cutoff);
  if (static_cast<int>(attempts.size()) >= rateLimit.maxAttempts) {
    return false;
  }
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
