// io_uring-based poller implementation
#include "IoUring.h"
#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <string.h>
#include <sys/epoll.h>
#include <iostream>
#include "Util.h"
#include "base/Logging.h"

using namespace std;

// io_uring CQE 中 user_data 编码：直接存 fd
// poll 事件使用 POLLIN/POLLOUT 等，需要与 EPOLLIN/EPOLLOUT 互转

// POLL 事件 → EPOLL 事件（数值恰好一致，但显式转换更安全）
__uint32_t IoUring::pollToEpollEvents(short poll_events) {
  __uint32_t ep = 0;
  if (poll_events & POLLIN) ep |= EPOLLIN;
  if (poll_events & POLLOUT) ep |= EPOLLOUT;
  if (poll_events & POLLERR) ep |= EPOLLERR;
  if (poll_events & POLLHUP) ep |= EPOLLHUP;
  if (poll_events & POLLPRI) ep |= EPOLLPRI;
  if (poll_events & POLLRDHUP) ep |= EPOLLRDHUP;
  return ep;
}

// EPOLL 事件 → POLL 事件（用于提交 POLL_ADD）
static short epollToPollEvents(__uint32_t epoll_events) {
  short p = 0;
  if (epoll_events & EPOLLIN) p |= POLLIN;
  if (epoll_events & EPOLLOUT) p |= POLLOUT;
  if (epoll_events & EPOLLERR) p |= POLLERR;
  if (epoll_events & EPOLLHUP) p |= POLLHUP;
  if (epoll_events & EPOLLPRI) p |= POLLPRI;
  if (epoll_events & EPOLLRDHUP) p |= POLLRDHUP;
  return p;
}


// 辅助函数：将 fd 和操作类型打包到 64 位 user_data 中
static uint64_t packUserData(int fd, UringOp op) {
    return ((uint64_t)op << 32) | (uint32_t)fd;
}

static void unpackUserData(uint64_t data, int& fd, UringOp& op) {
    fd = (int)(data & 0xFFFFFFFF);
    op = (UringOp)(data >> 32);
}

IoUring::IoUring() {
  memset(fd2events_, 0, sizeof(fd2events_));

  // 初始化 io_uring，启用 SQPOLL 可进一步减少系统调用
  // 这里先用普通模式，稳定后可加 IORING_SETUP_SQPOLL
  int ret = io_uring_queue_init(RING_SIZE, &ring_, 0);
  if (ret < 0) {
    LOG << "io_uring_queue_init failed: " << strerror(-ret);
    abort();
  }
}

IoUring::~IoUring() {
  io_uring_queue_exit(&ring_);
}

void IoUring::submitPollAdd(int fd, __uint32_t epoll_events) {
  struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
  if (!sqe) {
    // SQ 满了，先同步提交一批
    io_uring_submit(&ring_);
    sqe = io_uring_get_sqe(&ring_);
    if (!sqe) {
      LOG << "io_uring SQ full, cannot add poll for fd=" << fd;
      return;
    }
  }

  // POLL_ADD：让内核监听该 fd 上的事件
  // 注意 io_uring poll 使用的是 poll(2) 风格事件，不是 epoll 风格
  io_uring_prep_poll_add(sqe, fd, epollToPollEvents(epoll_events));
  // user_data 存 fd，在 CQE 中用来识别是哪个 fd 的事件
  io_uring_sqe_set_data64(sqe, packUserData(fd, UringOp::POLL_ADD));

  fd2events_[fd] = epoll_events;
}

void IoUring::cancelPoll(int fd) {
  struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
  if (!sqe) {
    io_uring_submit(&ring_);
    sqe = io_uring_get_sqe(&ring_);
    if (!sqe) return;
  }

  // POLL_REMOVE：按 user_data 取消之前注册的 POLL_ADD
  io_uring_prep_poll_remove(sqe, packUserData(fd, UringOp::POLL_ADD));
  io_uring_sqe_set_data64(sqe, (uint64_t)(-1));  // 标记为取消操作
}

// 注册新描述符 — 完全兼容 Epoll::epoll_add 的语义
void IoUring::epoll_add(SP_Channel request, int timeout) {
  int fd = request->getFd();
  if (timeout > 0) {
    add_timer(request, timeout);
    fd2http_[fd] = request->getHolder();
  }

  fd2chan_[fd] = request;
  request->EqualAndUpdateLastEvents();

  // 提交 POLL_ADD，如果关注事件为 0，则仅加入追踪，不实际 poll
  if (request->getEvents() != 0) {
      submitPollAdd(fd, request->getEvents());
  }
}


// 修改描述符状态 — 兼容 Epoll::epoll_mod
void IoUring::epoll_mod(SP_Channel request, int timeout) {
  if (timeout > 0) add_timer(request, timeout);
  int fd = request->getFd();

  if (!request->EqualAndUpdateLastEvents()) {
    // 事件发生了变化，需要取消旧的 poll 并提交新的
    cancelPoll(fd);
    submitPollAdd(fd, request->getEvents());
  }
}

// 删除描述符 — 兼容 Epoll::epoll_del
void IoUring::epoll_del(SP_Channel request) {
  int fd = request->getFd();

  // 取消 poll 监听
  cancelPoll(fd);

  fd2chan_[fd].reset();
  fd2http_[fd].reset();
  fd2events_[fd] = 0;
}

// 返回活跃事件 — 兼容 Epoll::poll

void IoUring::submitRead(SP_Channel request, void* buffer, size_t len) {
    int fd = request->getFd();
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
    if (!sqe) {
        io_uring_submit(&ring_);
        sqe = io_uring_get_sqe(&ring_);
        if (!sqe) return;
    }
    
    io_uring_prep_read(sqe, fd, buffer, len, 0);
    io_uring_sqe_set_data64(sqe, packUserData(fd, UringOp::ASYNC_READ));
}

void IoUring::submitWrite(SP_Channel request, const void* buffer, size_t len) {
    int fd = request->getFd();
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
    if (!sqe) {
        io_uring_submit(&ring_);
        sqe = io_uring_get_sqe(&ring_);
        if (!sqe) return;
    }
    
    io_uring_prep_write(sqe, fd, buffer, len, 0);
    io_uring_sqe_set_data64(sqe, packUserData(fd, UringOp::ASYNC_WRITE));
}

void IoUring::processEvents() {
    while (true) {
        io_uring_submit(&ring_);

        struct __kernel_timespec ts;
        ts.tv_sec = 10;
        ts.tv_nsec = 0;

        struct io_uring_cqe* cqe;
        int ret = io_uring_wait_cqe_timeout(&ring_, &cqe, &ts);

        if (ret < 0) {
            if (ret == -EINTR) continue;
            if (ret == -ETIME) return; // timeout
            LOG << "io_uring_wait_cqe_timeout error: " << strerror(-ret);
            return;
        }

        struct io_uring_cqe* cqes[4096];
        int count = io_uring_peek_batch_cqe(&ring_, cqes, 4096);

        if (count > 0) {
            for (int i = 0; i < count; ++i) {
                uint64_t data = io_uring_cqe_get_data64(cqes[i]);
                if (data == (uint64_t)-1) continue;
                
                int fd;
                UringOp op;
                unpackUserData(data, fd, op);

                if (cqes[i]->res < 0 && cqes[i]->res != -ECANCELED) {
                    // LOG error
                }

                if (fd >= 0 && fd < MAXFDS) {
                    SP_Channel cur_req = fd2chan_[fd];
                    if (cur_req) {
                        if (op == UringOp::POLL_ADD) {
                            __uint32_t epoll_events = pollToEpollEvents(cqes[i]->res);
                            cur_req->setRevents(epoll_events);
                            cur_req->setEvents(0);
                            cur_req->handleEvents();
                        } else if (op == UringOp::ASYNC_READ) {
                            cur_req->handleReadComplete(cqes[i]->res);
                        } else if (op == UringOp::ASYNC_WRITE) {
                            cur_req->handleWriteComplete(cqes[i]->res);
                        }
                    }
                }
            }
            io_uring_cq_advance(&ring_, count);
            return;
        }
    }
}

void IoUring::add_timer(SP_Channel request_data, int timeout) {
  shared_ptr<HttpData> t = request_data->getHolder();
  if (t)
    timerManager_.addTimer(t, timeout);
  else
    LOG << "timer add fail";
}
