#include "httpserver/application/StructuredLog.h"

#include <chrono>
#include <cstdlib>

#include "httpserver/base/Logging.h"
#include "json.hpp"

namespace httpserver {

void LogStructuredEvent(const StructuredLogEvent& event) {
  const char* configuredInstance = std::getenv("HTTPSERVER_INSTANCE_ID");
  const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
  const nlohmann::json output = {
      {"timestamp_ms", now},
      {"level", "info"},
      {"instance_id", configuredInstance ? configuredInstance : "local"},
      {"request_id", event.requestId},
      {"connection_id", event.connectionId},
      {"event_id", event.eventId},
      {"message_id", event.messageId},
      {"operation", event.operation},
      {"duration_ms", event.durationMilliseconds},
      {"error_code", event.errorCode},
  };
  LOG << output.dump();
}

}  // namespace httpserver
