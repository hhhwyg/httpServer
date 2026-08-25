#pragma once

#include <string>
#include <string_view>

namespace httpserver {

class MimeType {
 public:
  static std::string getMime(std::string_view suffix);
};

}  // namespace httpserver
