#include <cassert>
#include <string>

#include "httpserver/protocol/HttpResponse.h"

int main() {
  const std::string keepAlive = httpserver::HttpResponseWriter::SerializeHead(
      200, "text/plain", 2, true);
  assert(keepAlive.find("HTTP/1.1 200 OK\r\n") == 0);
  assert(keepAlive.find("Content-Length: 2\r\n") != std::string::npos);
  assert(keepAlive.find("Connection: Keep-Alive\r\n") != std::string::npos);
  assert(keepAlive.find("Keep-Alive: timeout=20\r\n") != std::string::npos);

  const std::string closed = httpserver::HttpResponseWriter::SerializeHead(
      413, "text/plain", 0, false);
  assert(closed.find("HTTP/1.1 413 Payload Too Large\r\n") == 0);
  assert(closed.find("Connection: close\r\n") != std::string::npos);
  assert(closed.find("Keep-Alive:") == std::string::npos);
  return 0;
}
