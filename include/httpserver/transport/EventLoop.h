#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <sys/types.h>
#include <utility>
#include <vector>

#include "httpserver/transport/Channel.h"
#include "httpserver/transport/Poller.h"

namespace httpserver {

class EventLoop {
 public:
  using Functor = std::function<void()>;

  explicit EventLoop(
      PollerBackend backend = PollerBackend::kIoUring,
      const PollerConfig& config = {});
  ~EventLoop();

  void loop();
  void quit();
  void runInLoop(Functor&& cb);
  void queueInLoop(Functor&& cb);
  bool isInLoopThread() const;
  void assertInLoopThread();

  void shutdown(ChannelPtr channel);
  void removeFromPoller(ChannelPtr channel);
  void updatePoller(ChannelPtr channel, int timeout = 0);
  void addToPoller(ChannelPtr channel, int timeout = 0);
  void submitRead(ChannelPtr channel, void* buffer, std::size_t len);
  void submitWrite(ChannelPtr channel, const void* buffer, std::size_t len);

 private:
  bool looping_;
  std::unique_ptr<Poller> poller_;
  int wakeupFd_;
  std::atomic<bool> quit_;
  bool eventHandling_;
  mutable std::mutex mutex_;
  std::vector<Functor> pendingFunctors_;
  bool callingPendingFunctors_;
  const pid_t threadId_;
  ChannelPtr pwakeupChannel_;

  void wakeup();
  void handleRead();
  void doPendingFunctors();
  void handleConn();
};

}  // namespace httpserver
