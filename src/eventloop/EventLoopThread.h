#pragma once
#include "EventLoop.h"
#include "base/Condition.h"
#include "base/MutexLock.h"
#include "base/Thread.h"
#include "base/noncopyable.h"


class EventLoopThread : noncopyable {
 public:
  explicit EventLoopThread(PollerBackend backend);
  ~EventLoopThread();
  EventLoop* startLoop();

 private:
  void threadFunc();
  EventLoop* loop_;
  PollerBackend backend_;
  bool exiting_;
  Thread thread_;
  MutexLock mutex_;
  Condition cond_;
};
