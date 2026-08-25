#pragma once
#include "EventLoop.h"
#include "base/Condition.h"
#include "base/MutexLock.h"
#include "base/Thread.h"
#include "base/noncopyable.h"


class EventLoopThread : noncopyable {
 public:
  EventLoopThread(PollerBackend backend, PollerConfig config);
  ~EventLoopThread();
  EventLoop* startLoop();

 private:
  void threadFunc();
  EventLoop* loop_;
  PollerBackend backend_;
  PollerConfig config_;
  bool exiting_;
  Thread thread_;
  MutexLock mutex_;
  Condition cond_;
};
