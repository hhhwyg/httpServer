#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace httpserver {

enum class WebSocketDecodeStatus {
  kNeedMoreData,
  kOk,
  kProtocolError,
};

struct WebSocketFrame {
  bool fin = false;
  std::uint8_t opcode = 0;
  std::string payload;
  std::size_t bytesConsumed = 0;
};

struct WebSocketDecodeResult {
  WebSocketDecodeStatus status = WebSocketDecodeStatus::kNeedMoreData;
  WebSocketFrame frame;
  std::uint16_t closeCode = 1002;
  std::string errorReason;
};

class WebSocketCodec {
 public:
  static WebSocketDecodeResult DecodeFrame(std::string_view input);
  static std::string EncodeFrame(std::uint8_t opcode,
                                 std::string_view payload);
};

}  // namespace httpserver
