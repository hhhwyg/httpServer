#include <cassert>

#include "httpserver/protocol/HttpRequest.h"

int main() {
  const auto request = httpserver::HttpRequestParser::ParseRequestLine(
      "GET /index.html?token=abc HTTP/1.1");
  assert(request.has_value());
  assert(request->method == "GET");
  assert(request->target == "/index.html?token=abc");

  const auto headers = httpserver::HttpRequestParser::ParseHeaders(
      "Host: example.test\r\nContent-Length: 12\r\nConnection: keep-alive",
      "HTTP/1.1");
  assert(headers.has_value());
  assert(headers->values.at("host") == "example.test");
  assert(headers->contentLength == 12);
  assert(headers->keepAlive);

  assert(httpserver::HttpRequestParser::ParseRequestLine(
             "PUT / HTTP/1.1")
             .has_value());
  assert(!httpserver::HttpRequestParser::ParseRequestLine(
              "GET  / HTTP/1.1")
              .has_value());
  assert(httpserver::HttpRequestParser::QueryParam(
             "a=1&token=abc", "token") == "abc");
  return 0;
}
