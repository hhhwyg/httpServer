#pragma once

#include <cstdint>
#include <string_view>

namespace httpserver {

struct StructuredLogEvent {
  std::string_view requestId;
  std::string_view connectionId;
  std::string_view eventId;
  std::string_view messageId;
  std::string_view operation;
  std::string_view errorCode;
  std::uint64_t durationMilliseconds = 0;
};

// Emits a bounded JSON event. Callers must pass stable identifiers only; this
// deliberately has no fields for credentials, message bodies, or raw URIs.
void LogStructuredEvent(const StructuredLogEvent& event);

}  // namespace httpserver
