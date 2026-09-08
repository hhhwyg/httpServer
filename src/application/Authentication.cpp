#include "httpserver/application/Authentication.h"

#include "CryptoUtil.h"
#include "httpserver/application/RedisClient.h"
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
  const auto claims = VerifyClaims(query, noHeaders);
  return claims.has_value() ? std::optional<std::string>(claims->username)
                            : std::nullopt;
}

std::optional<AuthenticatedToken> Authenticator::VerifyClaims(
    std::string_view query, const HeaderMap& headers) {
  const auto token = ExtractToken(query, headers);
  if (!token.has_value()) return std::nullopt;
  const auto claims = CryptoUtil::verifyAndExtractClaims(*token);
  if (!claims.has_value()) return std::nullopt;
  const RedisTokenState state = RedisService::TokenState(claims->jti);
  // A configured denylist is fail-closed: Redis trouble must not restore a
  // credential that the user has explicitly logged out.
  if (state == RedisTokenState::kRevoked ||
      (RedisService::enabled() && state == RedisTokenState::kUnavailable)) {
    return std::nullopt;
  }
  return AuthenticatedToken{claims->username, claims->jti, claims->expiresAt};
}

std::optional<std::string> Authenticator::VerifyRequest(
    std::string_view query, const HeaderMap& headers) {
  const auto claims = VerifyClaims(query, headers);
  return claims.has_value() ? std::optional<std::string>(claims->username)
                            : std::nullopt;
}

}  // namespace httpserver
