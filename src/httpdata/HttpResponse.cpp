#include "httpserver/protocol/HttpResponse.h"

#include <string>

namespace httpserver {
namespace {

std::string_view ReasonPhrase(int status) {
  switch (status) {
    case 200:
      return "OK";
    case 201:
      return "Created";
    case 400:
      return "Bad Request";
    case 401:
      return "Unauthorized";
    case 404:
      return "Not Found";
    case 405:
      return "Method Not Allowed";
    case 413:
      return "Payload Too Large";
    case 429:
      return "Too Many Requests";
    default:
      return "Service Unavailable";
  }
}

}  // namespace

std::string HttpResponseWriter::SerializeHead(int status,
                                              std::string_view contentType,
                                              std::size_t contentLength,
                                              bool keepAlive) {
  std::string header;
  header.reserve(256);
  header += "HTTP/1.1 " + std::to_string(status) + " " +
      std::string(ReasonPhrase(status)) + "\r\n";
  header += "Content-Type: " + std::string(contentType) + "\r\n";
  header += "Content-Length: " + std::to_string(contentLength) + "\r\n";
  header += "Connection: ";
  header += keepAlive ? "Keep-Alive\r\n" : "close\r\n";
  if (keepAlive) header += "Keep-Alive: timeout=20\r\n";
  header += "\r\n";
  return header;
}

}  // namespace httpserver
