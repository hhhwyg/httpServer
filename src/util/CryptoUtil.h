#pragma once

#include <string>

namespace CryptoUtil {

std::string generateJWT(const std::string& username);
bool verifyJWT(const std::string& token);
std::string base64UrlEncode(const std::string& input);
std::string base64UrlDecode(const std::string& input);
std::string hmacSha256(const std::string& data, const std::string& key);
std::string extractUsername(const std::string& token);

// Used for WebSocket Handshake
std::string computeAcceptKey(std::string contextKey);

} // namespace CryptoUtil
