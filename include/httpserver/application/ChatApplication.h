#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "httpserver/application/ApplicationContext.h"

namespace httpserver {

class ChatApplication {
 public:
  using SendMessage = std::function<void(const std::string&)>;

  static void HandleMessage(
      std::string_view payload,
      const std::shared_ptr<ConnectionSession>& user,
      const SendMessage& sendMessage);
  static void Leave(std::uint64_t sessionId);
};

}  // namespace httpserver
