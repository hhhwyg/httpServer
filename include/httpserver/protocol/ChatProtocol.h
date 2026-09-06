#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "httpserver/domain/ChatDomain.h"

namespace httpserver {

enum class ChatCommandType {
  kSubscribe,
  kSendMessage,
};

struct ChatCommand {
  ChatCommandType type = ChatCommandType::kSubscribe;
  std::string requestId;
  std::string roomId;
  std::string clientMessageId;
  std::string content;
};

struct ChatProtocolParseResult {
  bool versioned = false;
  std::optional<ChatCommand> command;
  std::string requestId;
  std::string errorCode;
  std::string errorMessage;
};

class ChatProtocol {
 public:
  static constexpr int kVersion = 1;
  static constexpr std::size_t kMaxIdentifierBytes = 128;
  static constexpr std::size_t kMaxContentBytes = 4096;

  static ChatProtocolParseResult Parse(std::string_view payload);
  static std::string Subscribed(std::string_view requestId,
                                std::string_view roomId);
  static std::string MessageAck(std::string_view requestId,
                                const Message& message);
  static std::string MessageCreated(const Message& message);
  static std::string Error(std::string_view requestId, std::string_view code,
                           std::string_view message);
};

}  // namespace httpserver
