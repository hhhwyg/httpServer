// io_uring-based poller, drop-in replacement for Epoll
#pragma once
#include <liburing.h>
#include <memory>
#include <unordered_map>
#include "IoUringOperation.h"
#include "Timer.h"
#include "httpserver/transport/Poller.h"

class IoUring : public httpserver::Poller {
 public:
  explicit IoUring(const httpserver::PollerConfig& config = {});
  ~IoUring() override;

  void add(httpserver::ChannelPtr channel, int timeout) override;
  void modify(httpserver::ChannelPtr channel, int timeout) override;
  void remove(httpserver::ChannelPtr channel) override;
  // 真正的异步 I/O 接口
  void submitRead(httpserver::ChannelPtr request, void* buffer,
                  std::size_t len) override;
  void submitWrite(httpserver::ChannelPtr request, const void* buffer,
                   std::size_t len) override;

  void processEvents() override;

  void addTimer(httpserver::ChannelPtr channel, int timeout);
  void handleExpired() override;
  const char* name() const override { return "io_uring"; }

 private:
  // io_uring poll 事件 → epoll 事件的转换
  static __uint32_t pollToEpollEvents(short poll_events);

  void submitPollAdd(const httpserver::ChannelPtr& request);

  // 取消正在进行的 poll 请求
  void cancelPoll(int fd);
  void cancelOperation(UringOp type, int fd);

  struct io_uring ring_;
  unsigned queueSize_;
  std::unordered_map<int, httpserver::ChannelPtr> channels_;
  std::unordered_map<httpserver::Channel*, std::shared_ptr<void>>
      connectionOwners_;
  UringOperationRegistry operations_;
  TimerManager timerManager_;
};
