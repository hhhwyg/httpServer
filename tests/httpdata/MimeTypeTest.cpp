#include <cassert>
#include <string>

#include "httpserver/protocol/MimeType.h"

int main() {
  assert(httpserver::MimeType::getMime(".html") == "text/html");
  assert(httpserver::MimeType::getMime(".json") == "application/json");
  assert(httpserver::MimeType::getMime(".css") == "text/css");
  assert(httpserver::MimeType::getMime(".js") == "application/javascript");
  assert(httpserver::MimeType::getMime(".svg") == "image/svg+xml");
  assert(httpserver::MimeType::getMime(".woff2") == "font/woff2");
  assert(httpserver::MimeType::getMime(".unknown") == "text/html");
  return 0;
}
