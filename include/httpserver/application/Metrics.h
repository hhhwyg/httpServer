#pragma once

#include <cstdint>
#include <string>

namespace httpserver {

class Metrics {
 public:
  static void ConnectionOpened();
  static void ConnectionClosed();
  static void RequestStarted();
  static void ResponseSent(int status);
  static void IoError();
  static std::string Render(int activeRooms, int databaseFree,
                            int databaseInUse);
  static void ResetForTesting();
};

}  // namespace httpserver
