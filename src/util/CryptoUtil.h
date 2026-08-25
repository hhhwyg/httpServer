#pragma once

#include <optional>
#include <string>

#include "httpserver/config/ServerConfig.h"

namespace CryptoUtil {

void configureJwt(httpserver::JwtConfig config);
bool jwtEnabled();
std::string generateJWT(const std::string& username);
bool verifyJWT(const std::string& token);
std::optional<std::string> verifyAndExtractUsername(const std::string& token);
std::string base64UrlEncode(const std::string& input);
std::string base64UrlDecode(const std::string& input);
std::string hmacSha256(const std::string& data, const std::string& key);
std::string hashPassword(const std::string& password);
bool verifyPassword(const std::string& password, const std::string& encodedHash);

// Used for WebSocket Handshake
std::string computeAcceptKey(std::string contextKey);

} // namespace CryptoUtil
