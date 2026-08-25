#include <cassert>
#include <string>

#include "httpserver/protocol/MimeType.h"

int main() {
  assert(httpserver::MimeType::getMime(".html") == "text/html");
  assert(httpserver::MimeType::getMime(".json") == "application/json");
  assert(httpserver::MimeType::getMime(".unknown") == "text/html");
  return 0;
}
