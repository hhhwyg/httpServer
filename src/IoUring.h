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

  // 与 Epoll 完全相同的接口
  void epoll_add(httpserver::ChannelPtr request, int timeout) override;
  void epoll_mod(httpserver::ChannelPtr request, int timeout) override;
  void epoll_del(httpserver::ChannelPtr request) override;
  // 真正的异步 I/O 接口
  void submitRead(httpserver::ChannelPtr request, void* buffer,
                  std::size_t len) override;
  void submitWrite(httpserver::ChannelPtr request, const void* buffer,
                   std::size_t len) override;

  void processEvents() override;

  void add_timer(httpserver::ChannelPtr request_data, int timeout);
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
  std::unordered_map<httpserver::Channel*, std::shared_ptr<HttpData>>
      connectionOwners_;
  UringOperationRegistry operations_;
  TimerManager timerManager_;
};
