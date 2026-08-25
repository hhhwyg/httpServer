#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace httpserver {

enum class StaticFileStatus {
  kOk,
  kNotFound,
  kTooLarge,
};

struct StaticFileResult {
  StaticFileStatus status = StaticFileStatus::kNotFound;
  std::string contentType;
  std::size_t contentLength = 0;
  std::string body;
};

class StaticFileService {
 public:
  explicit StaticFileService(std::filesystem::path root = "./wwwroot");

  StaticFileResult load(std::string_view requestPath,
                        bool includeBody) const;

 private:
  std::filesystem::path root_;
};

}  // namespace httpserver
