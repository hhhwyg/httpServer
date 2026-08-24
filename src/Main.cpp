#include <getopt.h>
#include <string>
#include"SqlConnPool.h"
#include "EventLoop.h"
#include "Server.h"
#include "base/Logging.h"

int main(int argc, char* argv[]) {
  int threadNum = 6;
  int port = 8088;
  std::string logPath = "./WebServer.log";
  PollerBackend backend = PollerBackend::kIoUring;

  // parse args
  int opt;
  const char* str = "t:l:p:b:";
  while ((opt = getopt(argc, argv, str)) != -1) {
    switch (opt) {
      case 't': {
        threadNum = atoi(optarg);
        break;
      }
      case 'l': {
        logPath = optarg;
        if (logPath.size() < 2 || optarg[0] != '/') {
          printf("logPath should start with \"/\"\n");
          abort();
        }
        break;
      }
      case 'p': {
        port = atoi(optarg);
        break;
      }
      case 'b': {
        const auto parsedBackend = ParsePollerBackend(optarg);
        if (!parsedBackend.has_value()) {
          printf("backend must be epoll or uring\\n");
          return 1;
        }
        backend = *parsedBackend;
        break;
      }
      default:
        break;
    }
  }
  Logger::setLogFileName(logPath);
// STL库在多线程上应用
#ifndef _PTHREADS
  LOG << "_PTHREADS is not defined !";
#endif
 //SqlConnPool::Instance()->Init("localhost", 3306, "root", "123456", "webserver", 12);
  LOG << "Starting server with " << PollerBackendName(backend) << " backend";
  EventLoop mainLoop(backend);  // 主线程只负责接收 socket 连接
  Server myHTTPServer(&mainLoop, threadNum, port, backend);
  myHTTPServer.start();
  mainLoop.loop();
  return 0;
}
