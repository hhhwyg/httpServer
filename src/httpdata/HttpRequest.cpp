#include "httpserver/protocol/HttpRequest.h"

#include <algorithm>
#include <cctype>

namespace {

constexpr std::size_t kMaxRequestLineBytes = 8192;
constexpr std::size_t kMaxHeaderBytes = 16384;
constexpr std::size_t kMaxHeaderCount = 100;
constexpr std::size_t kMaxBodyBytes = 1024 * 1024;

std::string toLower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });
  return value;
}

std::optional<std::size_t> parseUnsignedSize(std::string_view value,
                                              std::size_t maximum) {
  if (value.empty()) return std::nullopt;
  std::size_t result = 0;
  for (const unsigned char character : value) {
    if (!std::isdigit(character) ||
        result > (maximum - (character - '0')) / 10) {
      return std::nullopt;
    }
    result = result * 10 + (character - '0');
  }
  return result <= maximum ? std::optional<std::size_t>(result) : std::nullopt;
}

}  // namespace

namespace httpserver {

std::optional<ParsedRequestLine> HttpRequestParser::ParseRequestLine(
    std::string_view line) {
  if (line.empty() || line.size() > kMaxRequestLineBytes) {
    return std::nullopt;
  }

  const std::size_t firstSpace = line.find(' ');
  const std::size_t secondSpace = line.find(' ', firstSpace + 1);
  if (firstSpace == std::string_view::npos ||
      secondSpace == std::string_view::npos ||
      line.find(' ', secondSpace + 1) != std::string_view::npos) {
    return std::nullopt;
  }

  ParsedRequestLine parsed{
      std::string(line.substr(0, firstSpace)),
      std::string(line.substr(firstSpace + 1, secondSpace - firstSpace - 1)),
      std::string(line.substr(secondSpace + 1))};
  if (parsed.method.empty() ||
      !std::all_of(parsed.method.begin(), parsed.method.end(),
                   [](unsigned char character) {
                     return std::isupper(character) != 0;
                   }) ||
      parsed.target.empty() || parsed.target.size() > 2048 ||
      (parsed.version != "HTTP/1.1" && parsed.version != "HTTP/1.0")) {
    return std::nullopt;
  }
  return parsed;
}

std::optional<ParsedHeaders> HttpRequestParser::ParseHeaders(
    std::string_view block, std::string_view version) {
  if (block.size() > kMaxHeaderBytes) return std::nullopt;

  ParsedHeaders parsed;
  std::size_t start = 0;
  std::size_t count = 0;
  while (start < block.size()) {
    const std::size_t end = block.find("\r\n", start);
    const std::size_t lineEnd = end == std::string_view::npos ? block.size() : end;
    const std::string_view line = block.substr(start, lineEnd - start);
    const std::size_t colon = line.find(':');
    if (colon == std::string_view::npos || colon == 0 || ++count > kMaxHeaderCount) {
      return std::nullopt;
    }

    const std::string key = toLower(std::string(line.substr(0, colon)));
    const std::size_t valueStart = line.find_first_not_of(" \t", colon + 1);
    const std::string value = valueStart == std::string_view::npos
                                  ? std::string{}
                                  : std::string(line.substr(
                                        valueStart,
                                        line.find_last_not_of(" \t") - valueStart + 1));
    if (key.size() > 128 || value.size() > 8192) return std::nullopt;

    if (key == "content-length") {
      const auto length = parseUnsignedSize(value, kMaxBodyBytes);
      if (!length.has_value() && parsed.contentLength.has_value()) {
        return std::nullopt;
      }
      if (!length.has_value() ||
          (parsed.contentLength.has_value() &&
           parsed.contentLength != length)) {
        return std::nullopt;
      }
      parsed.contentLength = length;
    }
    parsed.values[key] = value;
    if (end == std::string_view::npos) break;
    start = end + 2;
  }

  const auto connection = parsed.values.find("connection");
  parsed.keepAlive = connection == parsed.values.end()
                         ? version == "HTTP/1.1"
                         : toLower(connection->second).find("keep-alive") !=
                               std::string::npos;
  return parsed;
}

std::string HttpRequestParser::QueryParam(std::string_view query,
                                          std::string_view key) {
  std::size_t start = 0;
  while (start <= query.size()) {
    const std::size_t end = query.find('&', start);
    const std::string_view item = query.substr(start, end - start);
    const std::size_t equal = item.find('=');
    if (equal != std::string_view::npos && item.substr(0, equal) == key) {
      return std::string(item.substr(equal + 1));
    }
    if (end == std::string_view::npos) break;
    start = end + 1;
  }
  return {};
}

}  // namespace httpserver
