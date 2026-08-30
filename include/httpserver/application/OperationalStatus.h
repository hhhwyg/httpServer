#pragma once

#include <string>

namespace httpserver {

class OperationalStatus {
 public:
  static bool ready();
  static std::string prometheusMetrics();
};

}  // namespace httpserver
