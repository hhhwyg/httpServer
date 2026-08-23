// io_uring-based poller, drop-in replacement for Epoll
#pragma once
#include <liburing.h>
#include <memory>
#include <unordered_map>
#include <vector>
#include "Channel.h"
#include "HttpData.h"
#include "Timer.h"

enum class UringOp {
    POLL_ADD,
    ASYNC_READ,
    ASYNC_WRITE
};

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

  // 获取完成事件，注意现在不再返回 Channel 数组，
  // 而是直接在内部调用 Channel 的完成回调，因此改为 processEvents
  void processEvents();

  std::vector<std::shared_ptr<Channel>> getEventsRequest(
      struct io_uring_cqe** cqes, int count);
  void add_timer(std::shared_ptr<Channel> request_data, int timeout);
  void handleExpired();

 private:
  // io_uring poll 事件 → epoll 事件的转换
  static __uint32_t pollToEpollEvents(short poll_events);

  // 提交一个 POLL_ADD SQE
  void submitPollAdd(int fd, __uint32_t epoll_events);

  // 取消正在进行的 poll 请求
  void cancelPoll(int fd);

  static const int MAXFDS = 100000;
  static const int RING_SIZE = 4096;

  struct io_uring ring_;
  std::shared_ptr<Channel> fd2chan_[MAXFDS];
  std::shared_ptr<HttpData> fd2http_[MAXFDS];
  // 跟踪每个 fd 当前关注的 epoll 风格事件，用于重新提交 poll
  __uint32_t fd2events_[MAXFDS];
  TimerManager timerManager_;
};
