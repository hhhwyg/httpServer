#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>

class HttpData;

namespace httpserver {

class ChatApplication {
 public:
  using SendMessage = std::function<void(const std::string&)>;

  static void HandleMessage(std::string_view payload,
                            int senderFd,
                            const std::shared_ptr<::HttpData>& user,
                            const SendMessage& sendMessage);
  static void Leave(int senderFd);
};

}  // namespace httpserver
