#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace httpserver {

class Metrics {
 public:
  static void ConnectionOpened();
  static void ConnectionClosed();
  static void RequestStarted();
  static void ResponseSent(int status);
  static void HttpRequestCompleted(std::string_view method,
                                   std::string_view path, int status,
                                   std::uint64_t durationMicroseconds);
  static void WebSocketConnectionOpened();
  static void WebSocketConnectionClosed();
  static void WebSocketMessage(std::string_view type, bool accepted);
  static void WebSocketBroadcast(std::uint64_t durationMicroseconds);
  static void IoError();
  static void OutputBufferHighWater();
  static void OutputMessageDropped();
  static void SlowConsumerClosed();
  static std::string Render(int activeRooms, int databaseFree,
                            int databaseInUse);
  static void ResetForTesting();
};

}  // namespace httpserver
