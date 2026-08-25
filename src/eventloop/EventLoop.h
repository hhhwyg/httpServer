#pragma once
#include <cstddef>
#include <functional>
#include <memory>
#include <vector>
#include "Util.h"
#include "base/CurrentThread.h"
#include "base/Thread.h"
#include "httpserver/transport/Channel.h"
#include "httpserver/transport/Poller.h"
#include "httpserver/base/Logging.h"

class EventLoop {
 public:
  typedef std::function<void()> Functor;
  explicit EventLoop(
      httpserver::PollerBackend backend = httpserver::PollerBackend::kIoUring,
      const httpserver::PollerConfig& config = {});
  ~EventLoop();
  void loop();
  void quit();
  void runInLoop(Functor&& cb);
  void queueInLoop(Functor&& cb);
  bool isInLoopThread() const { return threadId_ == CurrentThread::tid(); }
  void assertInLoopThread() { assert(isInLoopThread()); }
  void shutdown(httpserver::ChannelPtr channel) { shutDownWR(channel->getFd()); }
  void removeFromPoller(httpserver::ChannelPtr channel) {
    // shutDownWR(channel->getFd());
    poller_->epoll_del(channel);
  }
  void updatePoller(httpserver::ChannelPtr channel, int timeout = 0) {
    poller_->epoll_mod(channel, timeout);
  }
  void addToPoller(httpserver::ChannelPtr channel, int timeout = 0) {
    poller_->epoll_add(channel, timeout);
  }
  void submitRead(httpserver::ChannelPtr channel, void* buffer,
                  std::size_t len) {
    poller_->submitRead(channel, buffer, len);
  }
  void submitWrite(httpserver::ChannelPtr channel, const void* buffer,
                   std::size_t len) {
    poller_->submitWrite(channel, buffer, len);
  }

 private:
  // 声明顺序 wakeupFd_ > pwakeupChannel_
  bool looping_;
  std::unique_ptr<httpserver::Poller> poller_;
  int wakeupFd_;
  bool quit_;
  bool eventHandling_;
  mutable MutexLock mutex_;
  std::vector<Functor> pendingFunctors_;
  bool callingPendingFunctors_;
  const pid_t threadId_;
  httpserver::ChannelPtr pwakeupChannel_;

  void wakeup();
  void handleRead();
  void doPendingFunctors();
  void handleConn();
};
