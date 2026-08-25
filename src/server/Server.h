#pragma once
#include <memory>
#include "EventLoop.h"
#include "EventLoopThreadPool.h"
#include "httpserver/transport/Channel.h"

class Server {
 public:
  Server(EventLoop* loop, int threadNum, int port,
         httpserver::PollerBackend backend, httpserver::PollerConfig config);
  ~Server() {}
  EventLoop *getLoop() const { return loop_; }
  void start();
  void handNewConn();
  void handThisConn() { loop_->updatePoller(acceptChannel_); }

 private:
  EventLoop *loop_;
  int threadNum_;
  std::unique_ptr<EventLoopThreadPool> eventLoopThreadPool_;
  bool started_;
  httpserver::ChannelPtr acceptChannel_;
  int port_;
  int listenFd_;
  int maxFds_;
};
