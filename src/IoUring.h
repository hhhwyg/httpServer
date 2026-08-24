// io_uring-based poller, drop-in replacement for Epoll
#pragma once
#include <liburing.h>
#include <memory>
#include <unordered_map>
#include "IoUringOperation.h"
#include "Poller.h"
#include "Timer.h"

class IoUring : public Poller {
 public:
  IoUring();
  ~IoUring() override;

  // 与 Epoll 完全相同的接口
  void epoll_add(SP_Channel request, int timeout) override;
  void epoll_mod(SP_Channel request, int timeout) override;
  void epoll_del(SP_Channel request) override;
  // 真正的异步 I/O 接口
  void submitRead(SP_Channel request, void* buffer, std::size_t len) override;
  void submitWrite(SP_Channel request, const void* buffer,
                   std::size_t len) override;

  void processEvents() override;

  void add_timer(std::shared_ptr<Channel> request_data, int timeout);
  void handleExpired() override;
  const char* name() const override { return "io_uring"; }

 private:
  // io_uring poll 事件 → epoll 事件的转换
  static __uint32_t pollToEpollEvents(short poll_events);

  void submitPollAdd(const SP_Channel& request);

  // 取消正在进行的 poll 请求
  void cancelPoll(int fd);

  static constexpr unsigned kRingSize = 4096;

  struct io_uring ring_;
  std::unordered_map<int, SP_Channel> channels_;
  std::unordered_map<Channel*, std::shared_ptr<HttpData>> connectionOwners_;
  UringOperationRegistry operations_;
  TimerManager timerManager_;
};
