#pragma once

#include <cstddef>
#include <chrono>
#include <functional>

namespace httpserver {

class DatabaseExecutor {
 public:
  struct Config {
    std::size_t workerCount = 2;
    std::size_t queueCapacity = 256;
  };

  struct Stats {
    std::size_t queued = 0;
    std::size_t running = 0;
    std::size_t rejected = 0;
    std::size_t expired = 0;
  };

  // Configure before the first Submit call. The executor deliberately rejects
  // work when saturated so slow storage cannot consume unbounded memory.
  static bool Configure(Config config);
  static bool Submit(std::function<void()> task);
  // A queued task is discarded when it cannot start before this deadline.
  // The caller still owns the user-visible failure response.
  static bool Submit(std::function<void()> task,
                     std::chrono::milliseconds queueDeadline);
  static bool Submit(std::function<void()> task,
                     std::chrono::milliseconds queueDeadline,
                     std::function<void()> onExpired);
  static Stats GetStats();
  static void Shutdown();
};

}  // namespace httpserver
