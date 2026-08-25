#include <cassert>
#include <string>

#include "httpserver/protocol/WebSocket.h"

int main() {
  const std::string input =
      "\x81\x85\x01\x02\x03\x04\x69\x67\x6f\x68\x6e";
  const auto decoded = httpserver::WebSocketCodec::DecodeFrame(input);
  assert(decoded.status == httpserver::WebSocketDecodeStatus::kOk);
  assert(decoded.frame.fin);
  assert(decoded.frame.opcode == 1);
  assert(decoded.frame.payload == "hello");
  assert(decoded.frame.bytesConsumed == input.size());

  const auto encoded = httpserver::WebSocketCodec::EncodeFrame(1, "hello");
  assert(encoded == "\x81\x05hello");

  const auto incomplete = httpserver::WebSocketCodec::DecodeFrame("\x81");
  assert(incomplete.status ==
         httpserver::WebSocketDecodeStatus::kNeedMoreData);

  const auto unmasked = httpserver::WebSocketCodec::DecodeFrame("\x81\x01x");
  assert(unmasked.status ==
         httpserver::WebSocketDecodeStatus::kProtocolError);
  return 0;
}
