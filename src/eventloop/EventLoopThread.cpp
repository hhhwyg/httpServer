#include "EventLoopThread.h"
#include <cassert>
#include <functional>

EventLoopThread::EventLoopThread(httpserver::PollerBackend backend,
                                 httpserver::PollerConfig config)
    : loop_(NULL),
      backend_(backend),
      config_(config),
      exiting_(false),
      thread_(std::bind(&EventLoopThread::threadFunc, this), "EventLoopThread"),
      mutex_(),
      cond_(mutex_) {}

EventLoopThread::~EventLoopThread() {
  exiting_ = true;
  if (loop_ != NULL) {
    loop_->quit();
    thread_.join();
  }
}

httpserver::EventLoop* EventLoopThread::startLoop() {
  assert(!thread_.started());
  thread_.start();
  {
    MutexLockGuard lock(mutex_);
    // 一直等到threadFun在Thread里真正跑起来
    while (loop_ == NULL) cond_.wait();
  }
  return loop_;
}

void EventLoopThread::threadFunc() {
  httpserver::EventLoop loop(backend_, config_);

  {
    MutexLockGuard lock(mutex_);
    loop_ = &loop;
    cond_.notify();
  }

  loop.loop();
  // assert(exiting_);
  loop_ = NULL;
}
