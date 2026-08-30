#include "LogFile.h"
#include <assert.h>
#include <stdio.h>
#include <filesystem>
#include <time.h>
#include <vector>
#include "FileUtil.h"


using namespace std;

LogFile::LogFile(const string& basename, int flushEveryN, size_t rollSizeBytes,
                 int maxArchiveFiles)
    : basename_(basename),
      flushEveryN_(flushEveryN),
      rollSizeBytes_(rollSizeBytes),
      maxArchiveFiles_(maxArchiveFiles),
      count_(0),
      mutex_(new MutexLock) {
  // assert(basename.find('/') >= 0);
  file_.reset(new AppendFile(basename));
}

LogFile::~LogFile() {}

void LogFile::append(const char* logline, int len) {
  MutexLockGuard lock(*mutex_);
  append_unlocked(logline, len);
}

void LogFile::flush() {
  MutexLockGuard lock(*mutex_);
  file_->flush();
}

void LogFile::append_unlocked(const char* logline, int len) {
  file_->append(logline, len);
  if (file_->writtenBytes() >= rollSizeBytes_) {
    rollFile();
  }
  ++count_;
  if (count_ >= flushEveryN_) {
    count_ = 0;
    file_->flush();
  }
}

bool LogFile::rollFile() {
  namespace fs = std::filesystem;
  const time_t now = time(nullptr);
  tm localTime{};
  localtime_r(&now, &localTime);
  char suffix[32]{};
  strftime(suffix, sizeof(suffix), ".%Y%m%d-%H%M%S", &localTime);
  const fs::path source(basename_);
  fs::path target = basename_ + suffix;
  int index = 1;
  while (fs::exists(target)) target = basename_ + suffix + "." + to_string(index++);
  file_->flush();
  file_.reset();
  std::error_code error;
  fs::rename(source, target, error);
  if (error) fprintf(stderr, "LogFile rotation failed: %s\n", error.message().c_str());
  file_.reset(new AppendFile(basename_));
  vector<fs::path> archives;
  const fs::path directory = source.parent_path().empty() ? "." : source.parent_path();
  const string prefix = source.filename().string() + ".";
  for (const auto& entry : fs::directory_iterator(directory, error)) {
    if (!error && entry.path().filename().string().rfind(prefix, 0) == 0) {
      archives.push_back(entry.path());
    }
  }
  sort(archives.begin(), archives.end());
  while (static_cast<int>(archives.size()) > maxArchiveFiles_) {
    fs::remove(archives.front(), error);
    archives.erase(archives.begin());
  }
  return !error;
}
