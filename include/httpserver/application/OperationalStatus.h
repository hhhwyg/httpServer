#pragma once

#include <string>

namespace httpserver {

class OperationalStatus {
 public:
  // Called during bootstrap after schema validation. Subsequent probes run on
  // the database executor and update a cached readiness result.
  static void ConfigureDatabaseReadiness(bool databaseEnabled);
  static bool ready();
  static std::string prometheusMetrics();
};

}  // namespace httpserver
