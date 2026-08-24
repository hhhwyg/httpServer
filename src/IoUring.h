// io_uring-based poller, drop-in replacement for Epoll
#pragma once
#include <liburing.h>
#include <memory>
#include <unordered_map>
#include "Channel.h"
#include "IoUringOperation.h"
#include "Timer.h"

class IoUring {
 public:
  IoUring();
  ~IoUring();

  // 与 Epoll 完全相同的接口
  void epoll_add(SP_Channel request, int timeout);
  void epoll_mod(SP_Channel request, int timeout);
  void epoll_del(SP_Channel request);
  // 真正的异步 I/O 接口
  void submitRead(std::shared_ptr<Channel> request, void* buffer, size_t len);
  void submitWrite(std::shared_ptr<Channel> request, const void* buffer, size_t len);

  void processEvents();

  void add_timer(std::shared_ptr<Channel> request_data, int timeout);
  void handleExpired();

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
