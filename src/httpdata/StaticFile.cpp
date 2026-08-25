#include "httpserver/protocol/StaticFile.h"

#include <cstdint>
#include <fstream>
#include <iterator>
#include <sys/stat.h>
#include <utility>

#include "httpserver/protocol/MimeType.h"

namespace {

constexpr std::uintmax_t kMaxStaticFileBytes = 8 * 1024 * 1024;

bool hasPathTraversal(std::string_view path) {
  if (path.empty() || path.front() != '/' ||
      path.find('\\') != std::string_view::npos ||
      path.find('\0') != std::string_view::npos) {
    return true;
  }

  std::size_t begin = 1;
  while (begin <= path.size()) {
    const std::size_t end = path.find('/', begin);
    if (path.substr(begin, end - begin) == "..") return true;
    if (end == std::string_view::npos) break;
    begin = end + 1;
  }
  return false;
}

bool isWithinDirectory(const std::filesystem::path& root,
                       const std::filesystem::path& candidate) {
  auto rootPart = root.begin();
  auto candidatePart = candidate.begin();
  while (rootPart != root.end() && candidatePart != candidate.end()) {
    if (*rootPart != *candidatePart) return false;
    ++rootPart;
    ++candidatePart;
  }
  return rootPart == root.end();
}

}  // namespace

namespace httpserver {

StaticFileService::StaticFileService(std::filesystem::path root)
    : root_(std::move(root)) {}

StaticFileResult StaticFileService::load(std::string_view requestPath,
                                         bool includeBody) const {
  StaticFileResult result;
  if (hasPathTraversal(requestPath)) return result;

  const std::string fileName = requestPath == "/"
                                   ? "index.html"
                                   : std::string(requestPath.substr(1));
  if (fileName.empty()) return result;

  std::error_code filesystemError;
  const std::filesystem::path root =
      std::filesystem::canonical(root_, filesystemError);
  const std::filesystem::path requested =
      std::filesystem::canonical(root / fileName, filesystemError);
  if (filesystemError || !isWithinDirectory(root, requested)) return result;

  const std::string path = requested.string();
  struct stat metadata {};
  if (stat(path.c_str(), &metadata) < 0 || !S_ISREG(metadata.st_mode)) {
    return result;
  }
  if (metadata.st_size < 0 ||
      static_cast<std::uintmax_t>(metadata.st_size) > kMaxStaticFileBytes) {
    result.status = StaticFileStatus::kTooLarge;
    return result;
  }

  result.status = StaticFileStatus::kOk;
  result.contentLength = static_cast<std::size_t>(metadata.st_size);
  const std::size_t dot = path.find_last_of('.');
  result.contentType = dot == std::string::npos
                           ? "application/octet-stream"
                           : MimeType::getMime(path.substr(dot));
  if (!includeBody) return result;

  std::ifstream file(path, std::ios::binary);
  result.body.assign(std::istreambuf_iterator<char>(file),
                     std::istreambuf_iterator<char>());
  if (result.body.size() != result.contentLength) {
    result = {};
  }
  return result;
}

}  // namespace httpserver
