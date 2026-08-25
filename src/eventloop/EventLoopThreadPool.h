#pragma once
#include <memory>
#include <vector>
#include "EventLoopThread.h"
#include "base/noncopyable.h"
#include "httpserver/base/Logging.h"


class EventLoopThreadPool : noncopyable {
 public:
  EventLoopThreadPool(EventLoop* baseLoop, int numThreads,
                      httpserver::PollerBackend backend,
                      httpserver::PollerConfig config);

  ~EventLoopThreadPool() { LOG << "~EventLoopThreadPool()"; }
  void start();

  EventLoop* getNextLoop();

 private:
  EventLoop* baseLoop_;
  bool started_;
  int numThreads_;
  int next_;
  httpserver::PollerBackend backend_;
  httpserver::PollerConfig config_;
  std::vector<std::shared_ptr<EventLoopThread>> threads_;
  std::vector<EventLoop*> loops_;
};
