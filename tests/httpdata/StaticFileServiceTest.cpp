#include <cassert>
#include <filesystem>

#include "httpserver/protocol/StaticFile.h"

int main() {
  httpserver::StaticFileService service(std::filesystem::current_path() /
                                         "wwwroot");
  const auto index = service.load("/", true);
  assert(index.status == httpserver::StaticFileStatus::kOk);
  assert(index.contentLength == index.body.size());
  assert(index.contentType == "text/html");

  const auto traversal = service.load("/../README.md", true);
  assert(traversal.status == httpserver::StaticFileStatus::kNotFound);
  const auto missing = service.load("/does-not-exist", true);
  assert(missing.status == httpserver::StaticFileStatus::kNotFound);
  return 0;
}
