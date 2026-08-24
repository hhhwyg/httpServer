#include <cassert>
#include <string>

#include "CryptoUtil.h"

namespace {

JwtConfig testJwtConfig() {
  JwtConfig config;
  config.secret = "0123456789abcdef0123456789abcdef0123456789abcdef";
  config.issuer = "httpserver-test";
  config.ttlSeconds = 60;
  return config;
}

void testWebSocketAcceptKeyUsesStandardBase64() {
  assert(CryptoUtil::computeAcceptKey("dGhlIHNhbXBsZSBub25jZQ==") ==
         "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}

void testJwtValidation() {
  CryptoUtil::configureJwt(testJwtConfig());
  const std::string token = CryptoUtil::generateJWT("alice");
  assert(!token.empty());
  assert(CryptoUtil::verifyJWT(token));
  const auto subject = CryptoUtil::verifyAndExtractUsername(token);
  assert(subject.has_value());
  assert(*subject == "alice");

  std::string tampered = token;
  tampered.back() = tampered.back() == 'a' ? 'b' : 'a';
  assert(!CryptoUtil::verifyJWT(tampered));

  JwtConfig expiredConfig = testJwtConfig();
  expiredConfig.ttlSeconds = -1;
  CryptoUtil::configureJwt(expiredConfig);
  assert(!CryptoUtil::verifyJWT(CryptoUtil::generateJWT("alice")));
}

void testScryptPasswordHashing() {
  const std::string first = CryptoUtil::hashPassword("correct horse battery staple");
  const std::string second = CryptoUtil::hashPassword("correct horse battery staple");
  assert(!first.empty());
  assert(!second.empty());
  assert(first != second);
  assert(CryptoUtil::verifyPassword("correct horse battery staple", first));
  assert(!CryptoUtil::verifyPassword("wrong password", first));
  assert(!CryptoUtil::verifyPassword("correct horse battery staple", "plain-text"));
}

}  // namespace

int main() {
  testWebSocketAcceptKeyUsesStandardBase64();
  testJwtValidation();
  testScryptPasswordHashing();
  return 0;
}
