#include "httpserver/application/Authentication.h"

#include "CryptoUtil.h"
#include "httpserver/protocol/HttpRequest.h"

namespace httpserver {

std::optional<std::string> Authenticator::ExtractToken(
    std::string_view query, const HeaderMap& headers) {
  const std::string queryToken = HttpRequestParser::QueryParam(query, "token");
  if (!queryToken.empty()) return queryToken;

  const auto authorization = headers.find("authorization");
  constexpr std::string_view prefix = "Bearer ";
  if (authorization != headers.end() &&
      authorization->second.size() > prefix.size() &&
      authorization->second.compare(0, prefix.size(), prefix) == 0) {
    return authorization->second.substr(prefix.size());
  }
  return std::nullopt;
}

std::optional<std::string> Authenticator::VerifyQuery(std::string_view query) {
  const HeaderMap noHeaders;
  const auto token = ExtractToken(query, noHeaders);
  return token.has_value() ? CryptoUtil::verifyAndExtractUsername(*token)
                           : std::nullopt;
}

std::optional<std::string> Authenticator::VerifyRequest(
    std::string_view query, const HeaderMap& headers) {
  const auto token = ExtractToken(query, headers);
  return token.has_value() ? CryptoUtil::verifyAndExtractUsername(*token)
                           : std::nullopt;
}

}  // namespace httpserver
