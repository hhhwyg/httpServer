#include <cassert>
#include <string>

#include "httpserver/protocol/ChatProtocol.h"

namespace {

void TestValidCommands() {
  const auto subscribe = httpserver::ChatProtocol::Parse(
      R"({"version":1,"type":"room.subscribe","requestId":"r1","payload":{"roomId":"100"}})");
  assert(subscribe.command.has_value());
  assert(subscribe.command->type == httpserver::ChatCommandType::kSubscribe);
  assert(subscribe.command->roomId == "100");

  const auto send = httpserver::ChatProtocol::Parse(
      R"({"version":1,"type":"message.send","requestId":"r2","payload":{"roomId":"100","clientMessageId":"c1","content":"hello"}})");
  assert(send.command.has_value());
  assert(send.command->type == httpserver::ChatCommandType::kSendMessage);
  assert(send.command->clientMessageId == "c1");
}

void TestProtocolErrors() {
  const auto missingField = httpserver::ChatProtocol::Parse(
      R"({"version":1,"type":"message.send","requestId":"r1","payload":{"roomId":"100"}})");
  assert(missingField.errorCode == "invalid_client_message_id");

  const auto wrongType = httpserver::ChatProtocol::Parse(
      R"({"version":1,"type":"message.send","requestId":"r1","payload":{"roomId":100,"clientMessageId":"c1","content":"hello"}})");
  assert(wrongType.errorCode == "invalid_room_id");

  const auto unknownVersion = httpserver::ChatProtocol::Parse(
      R"({"version":2,"type":"message.send","requestId":"r1","payload":{}})");
  assert(unknownVersion.versioned);
  assert(unknownVersion.errorCode == "unsupported_version");

  const std::string oversized(4097, 'x');
  const auto tooLong = httpserver::ChatProtocol::Parse(
      "{\"version\":1,\"type\":\"message.send\",\"requestId\":\"r1\","
      "\"payload\":{\"roomId\":\"100\",\"clientMessageId\":\"c1\","
      "\"content\":\"" +
      oversized + "\"}}");
  assert(tooLong.errorCode == "invalid_content");
}

void TestServerEnvelopes() {
  const httpserver::Message message{"200", "100", "alice", "c1", "hello",
                                    "2026-09-06T10:00:00.000Z"};
  const std::string ack = httpserver::ChatProtocol::MessageAck("r1", message);
  assert(ack.find("\"version\":1") != std::string::npos);
  assert(ack.find("\"type\":\"message.ack\"") != std::string::npos);
  assert(ack.find("\"messageId\":\"200\"") != std::string::npos);
  assert(ack.find("\"clientMessageId\":\"c1\"") != std::string::npos);
  assert(ack.find("\"payload\":{\"message\"") == std::string::npos);
  const std::string push = httpserver::ChatProtocol::MessageCreated(message);
  assert(push.find("\"senderId\":\"alice\"") != std::string::npos);
  assert(push.find("\"createdAt\"") != std::string::npos);
}

}  // namespace

int main() {
  TestValidCommands();
  TestProtocolErrors();
  TestServerEnvelopes();
  return 0;
}
