#pragma once

#include <string>
#include <utility>

#include "httpserver/base/LogStream.h"

namespace httpserver {

class Logger {
 public:
  Logger(const char* fileName, int line);
  ~Logger();

  LogStream& stream() { return impl_.stream_; }

  static void setLogFileName(std::string fileName) {
    logFileName_ = std::move(fileName);
  }
  static std::string getLogFileName() { return logFileName_; }

 private:
  class Impl {
   public:
    Impl(const char* fileName, int line);
    void formatTime();

    LogStream stream_;
    int line_;
    std::string basename_;
  };

  Impl impl_;
  static std::string logFileName_;
};

}  // namespace httpserver

#define LOG httpserver::Logger(__FILE__, __LINE__).stream()
