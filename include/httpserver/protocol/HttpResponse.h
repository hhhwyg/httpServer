#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace httpserver {

class HttpResponseWriter {
 public:
  static std::string SerializeHead(int status,
                                   std::string_view contentType,
                                   std::size_t contentLength,
                                   bool keepAlive);
};

}  // namespace httpserver
