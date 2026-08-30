#include "FileUtil.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring> // 提供 strerror 函数
#include <cerrno>  // 提供 errno 变量

using namespace std;

AppendFile::AppendFile(string filename)
    : fp_(fopen(filename.c_str(), "ae")), writtenBytes_(0) {
  if (fp_ == nullptr) {
      // 打印具体原因：是权限不足 (Permission denied) 还是路径不存在？
      fprintf(stderr, "LogFile Error: fopen failed for %s. Reason: %s\n", 
              filename.c_str(), strerror(errno));
      // 这种基础资源失效，直接退出比段错误更专业
      exit(1); 
  }
  setbuffer(fp_, buffer_, sizeof buffer_);//让用户控制缓冲区的大小和生命周期内部定义的 buffer_ 替换了 FILE* 默认的缓冲区（通常是 8KB）。
  const long offset = ftell(fp_);
  if (offset > 0) writtenBytes_ = static_cast<size_t>(offset);
}

AppendFile::~AppendFile() { fclose(fp_); }

void AppendFile::append(const char* logline, const size_t len) {
  size_t n = this->write(logline, len);
  size_t remain = len - n;
  while (remain > 0) {
    size_t x = this->write(logline + n, remain);
    if (x == 0) {
      int err = ferror(fp_);
      if (err) fprintf(stderr, "AppendFile::append() failed !\n");
      break;
    }
    n += x;
    remain = len - n;
  }
  writtenBytes_ += n;
}

void AppendFile::flush() { fflush(fp_); }

size_t AppendFile::write(const char* logline, size_t len) {
  return fwrite_unlocked(logline, 1, len, fp_);//fwrite_unlocked 是非线程安全版本。因为在日志系统中，通常上层会有 AsyncLogging 保证只有一个线程负责写这个文件，所以这里去掉锁开销，速度极快。
}
