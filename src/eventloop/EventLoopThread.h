#pragma once
#include "httpserver/transport/EventLoop.h"
#include "base/Condition.h"
#include "base/MutexLock.h"
#include "base/Thread.h"
#include "base/noncopyable.h"


class EventLoopThread : noncopyable {
 public:
  EventLoopThread(httpserver::PollerBackend backend,
                  httpserver::PollerConfig config);
  ~EventLoopThread();
  httpserver::EventLoop* startLoop();

 private:
  void threadFunc();
  httpserver::EventLoop* loop_;
  httpserver::PollerBackend backend_;
  httpserver::PollerConfig config_;
  bool exiting_;
  Thread thread_;
  MutexLock mutex_;
  Condition cond_;
};
