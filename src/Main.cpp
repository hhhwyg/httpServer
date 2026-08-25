#include <getopt.h>
#include <string>
#include"SqlConnPool.h"
#include "config/ServerConfig.h"
#include "EventLoop.h"
#include "Server.h"
#include "base/Logging.h"
#include "CryptoUtil.h"

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
          printf("backend must be epoll or uring\n");
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
  const ServerConfig config = ServerConfig::LoadFromEnvironment();
  if (!config.valid()) {
    for (const std::string& error : config.errors()) {
      fprintf(stderr, "Configuration error: %s\n", error.c_str());
    }
    return 1;
  }
  CryptoUtil::configureJwt(config.jwt);
  const PollerConfig pollerConfig{config.limits.maxFds,
                                  config.limits.ioUringQueueSize};
// STL库在多线程上应用
#ifndef _PTHREADS
  LOG << "_PTHREADS is not defined !";
#endif
  if (config.database.enabled() &&
      !SqlConnPool::Instance()->Init(
          config.database.host.c_str(), config.database.port,
          config.database.user.c_str(), config.database.password.c_str(),
          config.database.database.c_str(), config.database.poolSize)) {
    LOG << "Database initialization failed";
    return 1;
  }
  if (!config.database.enabled()) {
    LOG << "Database authentication routes are disabled";
  }
  if (!config.jwt.enabled()) {
    LOG << "JWT authentication is disabled because HTTPSERVER_JWT_SECRET is unset";
  }
  LOG << "Starting server with " << PollerBackendName(backend) << " backend";
  EventLoop mainLoop(backend, pollerConfig);  // 主线程只负责接收 socket 连接
  Server myHTTPServer(&mainLoop, threadNum, port, backend, pollerConfig);
  myHTTPServer.start();
  mainLoop.loop();
  return 0;
}
