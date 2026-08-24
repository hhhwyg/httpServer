#include "CryptoUtil.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include "json.hpp"

using json = nlohmann::json;

namespace CryptoUtil {
namespace {

constexpr std::size_t kJwtMaxLength = 8192;
constexpr std::size_t kPasswordMinLength = 8;
constexpr std::size_t kPasswordMaxLength = 128;
constexpr std::size_t kScryptSaltLength = 16;
constexpr std::size_t kScryptHashLength = 32;
constexpr std::uint64_t kScryptN = 32768;
constexpr std::uint64_t kScryptR = 8;
constexpr std::uint64_t kScryptP = 1;
constexpr std::uint64_t kScryptMaxMemory = 64ULL * 1024ULL * 1024ULL;

JwtConfig& configuredJwt() {
  static JwtConfig config;
  return config;
}

bool constantTimeEqual(const std::string& left, const std::string& right) {
  return left.size() == right.size() &&
         CRYPTO_memcmp(left.data(), right.data(), left.size()) == 0;
}

bool isBase64Url(std::string_view input) {
  return std::all_of(input.begin(), input.end(), [](unsigned char character) {
    return (character >= 'A' && character <= 'Z') ||
           (character >= 'a' && character <= 'z') ||
           (character >= '0' && character <= '9') || character == '-' ||
           character == '_';
  });
}

std::optional<std::string> decodeBase64Url(std::string_view input) {
  if (!isBase64Url(input) || input.size() > kJwtMaxLength ||
      input.size() % 4 == 1) {
    return std::nullopt;
  }

  std::string normalized(input);
  std::replace(normalized.begin(), normalized.end(), '-', '+');
  std::replace(normalized.begin(), normalized.end(), '_', '/');
  while (normalized.size() % 4 != 0) {
    normalized.push_back('=');
  }

  BIO* decoder = BIO_new(BIO_f_base64());
  BIO* memory = BIO_new_mem_buf(normalized.data(),
                                static_cast<int>(normalized.size()));
  if (!decoder || !memory) {
    BIO_free(decoder);
    BIO_free(memory);
    return std::nullopt;
  }

  decoder = BIO_push(decoder, memory);
  BIO_set_flags(decoder, BIO_FLAGS_BASE64_NO_NL);
  std::string decoded((normalized.size() / 4) * 3, '\0');
  const int length =
      decoded.empty() ? 0 : BIO_read(decoder, decoded.data(), decoded.size());
  BIO_free_all(decoder);
  if (length < 0) {
    return std::nullopt;
  }
  decoded.resize(static_cast<std::size_t>(length));
  return decoded;
}

std::string base64Encode(const unsigned char* input, std::size_t length) {
  BIO* encoder = BIO_new(BIO_f_base64());
  BIO* memory = BIO_new(BIO_s_mem());
  if (!encoder || !memory) {
    BIO_free(encoder);
    BIO_free(memory);
    return {};
  }

  encoder = BIO_push(encoder, memory);
  BIO_set_flags(encoder, BIO_FLAGS_BASE64_NO_NL);
  if (BIO_write(encoder, input, static_cast<int>(length)) <= 0 ||
      BIO_flush(encoder) != 1) {
    BIO_free_all(encoder);
    return {};
  }
  BUF_MEM* buffer = nullptr;
  BIO_get_mem_ptr(encoder, &buffer);
  std::string encoded(buffer->data, buffer->length);
  BIO_free_all(encoder);
  return encoded;
}

std::vector<std::string_view> split(std::string_view value, char delimiter) {
  std::vector<std::string_view> parts;
  std::size_t begin = 0;
  while (begin <= value.size()) {
    const std::size_t end = value.find(delimiter, begin);
    parts.push_back(value.substr(begin, end - begin));
    if (end == std::string_view::npos) {
      break;
    }
    begin = end + 1;
  }
  return parts;
}

bool deriveScrypt(const std::string& password,
                  const std::array<unsigned char, kScryptSaltLength>& salt,
                  std::array<unsigned char, kScryptHashLength>* output) {
  return EVP_PBE_scrypt(password.data(), password.size(), salt.data(),
                         salt.size(), kScryptN, kScryptR, kScryptP,
                         kScryptMaxMemory, output->data(), output->size()) == 1;
}

std::optional<std::string> verifiedUsername(const std::string& token,
                                             const JwtConfig& config) {
  if (!config.enabled() || token.empty() || token.size() > kJwtMaxLength) {
    return std::nullopt;
  }

  const std::vector<std::string_view> parts = split(token, '.');
  if (parts.size() != 3 || parts[0].empty() || parts[1].empty() ||
      parts[2].empty()) {
    return std::nullopt;
  }

  const std::string signingInput = std::string(parts[0]) + "." +
                                   std::string(parts[1]);
  const std::string expected =
      base64UrlEncode(hmacSha256(signingInput, config.secret));
  if (!constantTimeEqual(expected, std::string(parts[2]))) {
    return std::nullopt;
  }

  const auto decodedHeader = decodeBase64Url(parts[0]);
  const auto decodedPayload = decodeBase64Url(parts[1]);
  if (!decodedHeader.has_value() || !decodedPayload.has_value()) {
    return std::nullopt;
  }

  try {
    const json header = json::parse(*decodedHeader);
    const json payload = json::parse(*decodedPayload);
    if (!header.is_object() || !payload.is_object() ||
        header.value("alg", "") != "HS256" ||
        header.value("typ", "") != "JWT" ||
        payload.value("iss", "") != config.issuer ||
        !payload.contains("sub") || !payload["sub"].is_string() ||
        !payload.contains("iat") || !payload["iat"].is_number_integer() ||
        !payload.contains("exp") || !payload["exp"].is_number_integer()) {
      return std::nullopt;
    }

    const std::int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                                 std::chrono::system_clock::now()
                                     .time_since_epoch())
                                 .count();
    const std::int64_t issuedAt = payload["iat"].get<std::int64_t>();
    const std::int64_t expiresAt = payload["exp"].get<std::int64_t>();
    if (issuedAt > now + 60 || expiresAt <= now || expiresAt <= issuedAt) {
      return std::nullopt;
    }
    const std::string username = payload["sub"].get<std::string>();
    return username.empty() ? std::nullopt : std::optional<std::string>(username);
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

}  // namespace

void configureJwt(JwtConfig config) { configuredJwt() = std::move(config); }

bool jwtEnabled() { return configuredJwt().enabled(); }

std::string hmacSha256(const std::string& data, const std::string& key) {
  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned int length = 0;
  if (HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
           reinterpret_cast<const unsigned char*>(data.data()), data.size(), hash,
           &length) == nullptr) {
    return {};
  }
  return std::string(reinterpret_cast<const char*>(hash), length);
}

std::string base64UrlEncode(const std::string& input) {
  BIO* encoder = BIO_new(BIO_f_base64());
  BIO* memory = BIO_new(BIO_s_mem());
  if (!encoder || !memory) {
    BIO_free(encoder);
    BIO_free(memory);
    return {};
  }

  encoder = BIO_push(encoder, memory);
  BIO_set_flags(encoder, BIO_FLAGS_BASE64_NO_NL);
  if (BIO_write(encoder, input.data(), static_cast<int>(input.size())) <= 0 ||
      BIO_flush(encoder) != 1) {
    BIO_free_all(encoder);
    return {};
  }

  BUF_MEM* buffer = nullptr;
  BIO_get_mem_ptr(encoder, &buffer);
  std::string encoded(buffer->data, buffer->length);
  BIO_free_all(encoder);

  std::replace(encoded.begin(), encoded.end(), '+', '-');
  std::replace(encoded.begin(), encoded.end(), '/', '_');
  encoded.erase(std::remove(encoded.begin(), encoded.end(), '='), encoded.end());
  return encoded;
}

std::string base64UrlDecode(const std::string& input) {
  const auto decoded = decodeBase64Url(input);
  return decoded.value_or(std::string{});
}

std::string generateJWT(const std::string& username) {
  const JwtConfig& config = configuredJwt();
  if (!config.enabled() || username.empty()) {
    return {};
  }

  const std::int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();
  const json header = {{"alg", "HS256"}, {"typ", "JWT"}};
  const json payload = {{"sub", username},
                        {"iss", config.issuer},
                        {"iat", now},
                        {"exp", now + config.ttlSeconds}};
  const std::string signingInput = base64UrlEncode(header.dump()) + "." +
                                   base64UrlEncode(payload.dump());
  const std::string signature =
      base64UrlEncode(hmacSha256(signingInput, config.secret));
  return signature.empty() ? std::string{} : signingInput + "." + signature;
}

std::optional<std::string> verifyAndExtractUsername(const std::string& token) {
  return verifiedUsername(token, configuredJwt());
}

bool verifyJWT(const std::string& token) {
  return verifyAndExtractUsername(token).has_value();
}

std::string hashPassword(const std::string& password) {
  if (password.size() < kPasswordMinLength ||
      password.size() > kPasswordMaxLength) {
    return {};
  }

  std::array<unsigned char, kScryptSaltLength> salt{};
  std::array<unsigned char, kScryptHashLength> derived{};
  if (RAND_bytes(salt.data(), salt.size()) != 1 ||
      !deriveScrypt(password, salt, &derived)) {
    return {};
  }

  const std::string encodedSalt(
      reinterpret_cast<const char*>(salt.data()), salt.size());
  const std::string encodedDerived(
      reinterpret_cast<const char*>(derived.data()), derived.size());
  return "scrypt$v1$32768$8$1$" + base64UrlEncode(encodedSalt) + "$" +
         base64UrlEncode(encodedDerived);
}

bool verifyPassword(const std::string& password,
                    const std::string& encodedHash) {
  if (password.size() < kPasswordMinLength ||
      password.size() > kPasswordMaxLength) {
    return false;
  }

  const std::vector<std::string_view> parts = split(encodedHash, '$');
  if (parts.size() != 7 || parts[0] != "scrypt" || parts[1] != "v1" ||
      parts[2] != "32768" || parts[3] != "8" || parts[4] != "1") {
    return false;
  }
  const auto decodedSalt = decodeBase64Url(parts[5]);
  const auto decodedHash = decodeBase64Url(parts[6]);
  if (!decodedSalt.has_value() || !decodedHash.has_value() ||
      decodedSalt->size() != kScryptSaltLength ||
      decodedHash->size() != kScryptHashLength) {
    return false;
  }

  std::array<unsigned char, kScryptSaltLength> salt{};
  std::array<unsigned char, kScryptHashLength> expected{};
  std::copy(decodedSalt->begin(), decodedSalt->end(), salt.begin());
  if (!deriveScrypt(password, salt, &expected)) {
    return false;
  }

  return CRYPTO_memcmp(expected.data(), decodedHash->data(), expected.size()) == 0;
}

std::string computeAcceptKey(std::string contextKey) {
  const std::string input = contextKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  unsigned char hash[SHA_DIGEST_LENGTH];
  SHA1(reinterpret_cast<const unsigned char*>(input.data()), input.size(), hash);
  // RFC 6455 requires standard Base64 with padding here, unlike JWT's
  // Base64URL encoding used elsewhere in this file.
  return base64Encode(hash, SHA_DIGEST_LENGTH);
}

}  // namespace CryptoUtil
