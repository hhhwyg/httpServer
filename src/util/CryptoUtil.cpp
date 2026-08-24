#include "CryptoUtil.h"
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/buffer.h>
#include <algorithm>
#include "../json.hpp" // Note: adjust path if necessary, assuming it's in WebServer/

using json = nlohmann::json;

namespace CryptoUtil {

std::string hmacSha256(const std::string& data, const std::string& key) {
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int len = 0;

    HMAC(EVP_sha256(),
         key.data(), key.size(),
         (unsigned char*)data.data(), data.size(),
         hash, &len);

    return std::string((char*)hash, len);
}

std::string base64UrlEncode(const std::string& input) {
    BIO *b64 = BIO_new(BIO_f_base64());
    BIO *bmem = BIO_new(BIO_s_mem());
    b64 = BIO_push(b64, bmem);

    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL); 
    BIO_write(b64, input.data(), input.size());
    BIO_flush(b64);

    BUF_MEM *bptr;
    BIO_get_mem_ptr(b64, &bptr);

    std::string encoded(bptr->data, bptr->length);
    BIO_free_all(b64);

    for (auto &c : encoded) {
        if (c == '+') c = '-';
        else if (c == '/') c = '_';
    }

    encoded.erase(std::remove(encoded.begin(), encoded.end(), '='), encoded.end());

    return encoded;
}

std::string base64UrlDecode(const std::string& input) {
    std::string s = input;

    for (auto &c : s) {
        if (c == '-') c = '+';
        else if (c == '_') c = '/';
    }

    while (s.size() % 4 != 0) s += '=';

    BIO *b64 = BIO_new(BIO_f_base64());
    BIO *bmem = BIO_new_mem_buf(s.data(), s.size());
    bmem = BIO_push(b64, bmem);

    BIO_set_flags(bmem, BIO_FLAGS_BASE64_NO_NL);

    char buffer[2048];
    int len = BIO_read(bmem, buffer, sizeof(buffer));
    BIO_free_all(bmem);

    return std::string(buffer, len);
}

std::string generateJWT(const std::string& username) {
    std::string header = R"({"alg":"HS256","typ":"JWT"})";
    std::string payload = "{\"username\":\"" + username + "\"}";

    std::string encodedHeader = base64UrlEncode(header);
    std::string encodedPayload = base64UrlEncode(payload);

    std::string toSign = encodedHeader + "." + encodedPayload;
    std::string signature = base64UrlEncode(hmacSha256(toSign, "my_secret_key"));

    return encodedHeader + "." + encodedPayload + "." + signature;
}

bool verifyJWT(const std::string& token) {
    size_t p1 = token.find('.');
    size_t p2 = token.find('.', p1 + 1);
    if (p1 == std::string::npos || p2 == std::string::npos) return false;

    std::string header = token.substr(0, p1);
    std::string payload = token.substr(p1 + 1, p2 - p1 - 1);
    std::string signature = token.substr(p2 + 1);

    std::string toSign = header + "." + payload;
    std::string expected = base64UrlEncode(hmacSha256(toSign, "my_secret_key"));

    return expected == signature;
}

std::string extractUsername(const std::string& token) {
    size_t p1 = token.find('.');
    size_t p2 = token.find('.', p1 + 1);
    if (p1 == std::string::npos || p2 == std::string::npos)
        return "";

    std::string payload = token.substr(p1 + 1, p2 - p1 - 1);
    std::string jsonStr = base64UrlDecode(payload);

    try {
        auto j = json::parse(jsonStr);
        return j.value("username", "");
    } catch (...) {
        return "";
    }
}

std::string computeAcceptKey(std::string contextKey) {
    std::string magic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string input = contextKey + magic;

    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1((const unsigned char*)input.c_str(), input.length(), hash);

    BIO *b64 = BIO_new(BIO_f_base64());
    BIO *bmem = BIO_new(BIO_s_mem());
    b64 = BIO_push(b64, bmem);
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL); 
    BIO_write(b64, hash, SHA_DIGEST_LENGTH);
    BIO_flush(b64);
    BUF_MEM *bptr;
    BIO_get_mem_ptr(b64, &bptr);

    std::string output(bptr->data, bptr->length);
    BIO_free_all(b64);
    return output;
}

} // namespace CryptoUtil
