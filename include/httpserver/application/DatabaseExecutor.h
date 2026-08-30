#pragma once

#include <functional>

namespace httpserver {

class DatabaseExecutor {
 public:
  static bool Submit(std::function<void()> task);
  static void Shutdown();
};

}  // namespace httpserver
