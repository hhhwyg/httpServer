#include "EventLoopThreadPool.h"

EventLoopThreadPool::EventLoopThreadPool(EventLoop *baseLoop, int numThreads,
                                         httpserver::PollerBackend backend,
                                         httpserver::PollerConfig config)
    : baseLoop_(baseLoop),
      started_(false),
      numThreads_(numThreads),
      next_(0),
      backend_(backend),
      config_(config) {
  if (numThreads_ <= 0) {
    LOG << "numThreads_ <= 0";
    abort();
  }
}

//启动线程池
void EventLoopThreadPool::start() {
  baseLoop_->assertInLoopThread();
  started_ = true;
  for (int i = 0; i < numThreads_; ++i) {
    std::shared_ptr<EventLoopThread> t(new EventLoopThread(backend_, config_));
    threads_.push_back(t);
    loops_.push_back(t->startLoop());
  }
}
//负载均衡
EventLoop *EventLoopThreadPool::getNextLoop() {
  baseLoop_->assertInLoopThread();
  assert(started_);
  EventLoop *loop = baseLoop_;
  if (!loops_.empty()) {
    loop = loops_[next_];
    next_ = (next_ + 1) % numThreads_;
  }
  return loop;
}
