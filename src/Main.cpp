#include <chrono>
#include <getopt.h>
#include <pthread.h>
#include <signal.h>
#include <string>
#include <sys/signalfd.h>
#include <thread>
#include <unistd.h>
#include"SqlConnPool.h"
#include "httpserver/config/ServerConfig.h"
#include "httpserver/application/AuthenticationRateLimiter.h"
#include "httpserver/application/DatabaseExecutor.h"
#include "httpserver/transport/EventLoop.h"
#include "Server.h"
#include "httpserver/base/Logging.h"
#include "CryptoUtil.h"

int main(int argc, char* argv[]) {
  int threadNum = 6;
  int port = 8088;
  std::string logPath = "./WebServer.log";
  httpserver::PollerBackend backend = httpserver::PollerBackend::kIoUring;

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
        const auto parsedBackend = httpserver::ParsePollerBackend(optarg);
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
  httpserver::Logger::setLogFileName(logPath);
  const httpserver::ServerConfig config =
      httpserver::ServerConfig::LoadFromEnvironment();
  if (!config.valid()) {
    for (const std::string& error : config.errors()) {
      fprintf(stderr, "Configuration error: %s\n", error.c_str());
    }
    return 1;
  }
  CryptoUtil::configureJwt(config.jwt);
  httpserver::AuthenticationRateLimiter::Configure(
      config.authenticationRateLimit.maxAttempts,
      config.authenticationRateLimit.windowSeconds);
  const httpserver::PollerConfig pollerConfig{config.limits.maxFds,
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
  LOG << "Starting server with " << httpserver::PollerBackendName(backend)
      << " backend";
  httpserver::EventLoop mainLoop(backend, pollerConfig);  // 主线程只负责接收 socket 连接
  Server myHTTPServer(&mainLoop, threadNum, port, backend, pollerConfig);
  sigset_t shutdownSignals;
  sigemptyset(&shutdownSignals);
  sigaddset(&shutdownSignals, SIGINT);
  sigaddset(&shutdownSignals, SIGTERM);
  if (pthread_sigmask(SIG_BLOCK, &shutdownSignals, nullptr) != 0) {
    LOG << "Unable to block shutdown signals";
    return 1;
  }
  const int signalFd = signalfd(-1, &shutdownSignals, SFD_NONBLOCK | SFD_CLOEXEC);
  if (signalFd < 0) {
    LOG << "Unable to create shutdown signal fd";
    return 1;
  }
  myHTTPServer.start();
  bool shuttingDown = false;
  std::thread shutdownTimer;
  auto signalChannel = std::make_shared<httpserver::Channel>(&mainLoop, signalFd);
  signalChannel->setEvents(EPOLLIN);
  signalChannel->setReadHandler([&] {
    signalfd_siginfo signalInfo{};
    while (read(signalFd, &signalInfo, sizeof(signalInfo)) == sizeof(signalInfo)) {
      if (shuttingDown) continue;
      shuttingDown = true;
      LOG << "Shutdown signal received; stopping new connections";
      myHTTPServer.stopAccepting();
      shutdownTimer = std::thread([&mainLoop] {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        mainLoop.queueInLoop([&mainLoop] { mainLoop.quit(); });
      });
    }
  });
  mainLoop.addToPoller(signalChannel);
  mainLoop.loop();
  if (shutdownTimer.joinable()) shutdownTimer.join();
  mainLoop.removeFromPoller(signalChannel);
  close(signalFd);
  httpserver::DatabaseExecutor::Shutdown();
  return 0;
}
