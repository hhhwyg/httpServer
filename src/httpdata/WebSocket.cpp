#include "httpserver/protocol/WebSocket.h"

#include <limits>
#include <utility>

namespace httpserver {

WebSocketDecodeResult WebSocketCodec::DecodeFrame(std::string_view input) {
  WebSocketDecodeResult result;
  if (input.size() < 2) return result;

  const auto* data = reinterpret_cast<const unsigned char*>(input.data());
  const bool fin = (data[0] & 0x80) != 0;
  const std::uint8_t opcode = data[0] & 0x0f;
  const bool masked = (data[1] & 0x80) != 0;
  std::uint64_t payloadLength = data[1] & 0x7f;
  std::size_t headerLength = 2;

  if ((data[0] & 0x70) != 0 || !masked) {
    result.status = WebSocketDecodeStatus::kProtocolError;
    result.errorReason = "protocol error";
    return result;
  }
  if (payloadLength == 126) {
    if (input.size() < 4) return result;
    payloadLength = (static_cast<std::uint64_t>(data[2]) << 8) | data[3];
    headerLength = 4;
  } else if (payloadLength == 127) {
    if (input.size() < 10) return result;
    if ((data[2] & 0x80) != 0) {
      result.status = WebSocketDecodeStatus::kProtocolError;
      result.errorReason = "invalid length";
      return result;
    }
    payloadLength = 0;
    for (int index = 0; index < 8; ++index) {
      payloadLength = (payloadLength << 8) | data[2 + index];
    }
    headerLength = 10;
  }

  const bool control = opcode >= 0x8;
  constexpr std::uint64_t kMaxPayloadBytes = 1024 * 1024;
  if ((control && (!fin || payloadLength > 125)) ||
      payloadLength > kMaxPayloadBytes) {
    result.status = WebSocketDecodeStatus::kProtocolError;
    result.errorReason = "invalid frame";
    return result;
  }

  const std::uint64_t frameLength = headerLength + 4 + payloadLength;
  if (input.size() < frameLength) return result;

  const auto* mask = data + headerLength;
  const auto* body = mask + 4;
  std::string payload(static_cast<std::size_t>(payloadLength), '\0');
  for (std::size_t index = 0; index < payload.size(); ++index) {
    payload[index] = static_cast<char>(body[index] ^ mask[index % 4]);
  }

  result.status = WebSocketDecodeStatus::kOk;
  result.frame = {fin, opcode, std::move(payload),
                  static_cast<std::size_t>(frameLength)};
  return result;
}

std::string WebSocketCodec::EncodeFrame(std::uint8_t opcode,
                                        std::string_view payload) {
  if (payload.size() > std::numeric_limits<std::uint16_t>::max()) return {};

  std::string frame;
  frame.push_back(static_cast<char>(0x80 | opcode));
  if (payload.size() <= 125) {
    frame.push_back(static_cast<char>(payload.size()));
  } else {
    frame.push_back(126);
    frame.push_back(static_cast<char>((payload.size() >> 8) & 0xff));
    frame.push_back(static_cast<char>(payload.size() & 0xff));
  }
  frame.append(payload);
  return frame;
}

}  // namespace httpserver
