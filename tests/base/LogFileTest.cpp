#include <cassert>
#include <filesystem>

#include "LogFile.h"

int main() {
  namespace fs = std::filesystem;
  const fs::path active = "/tmp/httpserver-logfile-test.log";
  fs::remove(active);
  LogFile log(active.string(), 1, 4);
  log.append("first\n", 6);
  assert(fs::exists(active));

  int archives = 0;
  for (const auto& entry : fs::directory_iterator("/tmp")) {
    if (entry.path().filename().string().rfind("httpserver-logfile-test.log.", 0) == 0) {
      ++archives;
      fs::remove(entry.path());
    }
  }
  assert(archives >= 1);
  fs::remove(active);
  return 0;
}
