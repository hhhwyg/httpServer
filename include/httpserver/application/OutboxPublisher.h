#pragma once

#include <cstddef>
#include "httpserver/config/ServerConfig.h"

namespace httpserver {

// Publishes durable outbox records after their message transaction commits.
// It owns no EventLoop and never holds a database transaction during Redis I/O.
class OutboxPublisher {
 public:
  struct Stats {
    std::size_t claimed = 0;
    std::size_t published = 0;
    std::size_t failed = 0;
    std::size_t pending = 0;
    std::size_t oldestAgeSeconds = 0;
  };
  static void Configure(const OutboxConfig& config);
  static void Start();
  static void Shutdown();
  static Stats GetStats();
};

}  // namespace httpserver
