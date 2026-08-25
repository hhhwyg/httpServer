#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace httpserver {

struct ParsedRequestLine {
  std::string method;
  std::string target;
  std::string version;
};

struct ParsedHeaders {
  std::unordered_map<std::string, std::string> values;
  std::optional<std::size_t> contentLength;
  bool keepAlive = false;
};

class HttpRequestParser {
 public:
  static std::optional<ParsedRequestLine> ParseRequestLine(
      std::string_view line);
  static std::optional<ParsedHeaders> ParseHeaders(std::string_view block,
                                                   std::string_view version);
  static std::string QueryParam(std::string_view query, std::string_view key);
};

}  // namespace httpserver
