#include "IoUring.h"

#include <cerrno>
#include <cstring>
#include <poll.h>
#include <sys/epoll.h>
#include <vector>

#include "base/Logging.h"

namespace {

short epollToPollEvents(__uint32_t epollEvents) {
  // Channel 沿用 epoll 风格的兴趣事件；提交 IORING_OP_POLL_ADD 前必须
  // 转换成 poll(2) 事件位。EPOLLET/EPOLLONESHOT 没有对应的 poll 位。
  short pollEvents = 0;
  if (epollEvents & EPOLLIN) pollEvents |= POLLIN;
  if (epollEvents & EPOLLOUT) pollEvents |= POLLOUT;
  if (epollEvents & EPOLLERR) pollEvents |= POLLERR;
  if (epollEvents & EPOLLHUP) pollEvents |= POLLHUP;
  if (epollEvents & EPOLLPRI) pollEvents |= POLLPRI;
  if (epollEvents & EPOLLRDHUP) pollEvents |= POLLRDHUP;
  return pollEvents;
}

io_uring_sqe* getSqe(io_uring* ring) {
  io_uring_sqe* sqe = io_uring_get_sqe(ring);
  if (sqe != nullptr) {
    return sqe;
  }

  // SQ 满时先把已准备的 SQE 提交给内核，释放本地 SQ 空间；真正等待
  // CQE 的工作仍由 EventLoop 随后的 processEvents() 统一完成。
  const int submitResult = io_uring_submit(ring);
  if (submitResult < 0) {
    LOG << "io_uring_submit failed: " << std::strerror(-submitResult);
    return nullptr;
  }
  return io_uring_get_sqe(ring);
}

}  // namespace

__uint32_t IoUring::pollToEpollEvents(short pollEvents) {
  __uint32_t epollEvents = 0;
  if (pollEvents & POLLIN) epollEvents |= EPOLLIN;
  if (pollEvents & POLLOUT) epollEvents |= EPOLLOUT;
  if (pollEvents & POLLERR) epollEvents |= EPOLLERR;
  if (pollEvents & POLLHUP) epollEvents |= EPOLLHUP;
  if (pollEvents & POLLPRI) epollEvents |= EPOLLPRI;
  if (pollEvents & POLLRDHUP) epollEvents |= EPOLLRDHUP;
  return epollEvents;
}

IoUring::IoUring(const PollerConfig& config) : queueSize_(config.ioUringQueueSize) {
  const int result = io_uring_queue_init(queueSize_, &ring_, 0);
  if (result < 0) {
    LOG << "io_uring_queue_init failed: " << std::strerror(-result);
    std::abort();
  }
}

IoUring::~IoUring() { io_uring_queue_exit(&ring_); }

void IoUring::submitPollAdd(const SP_Channel& request) {
  if (!request || !request->isActive() || request->getEvents() == 0) {
    return;
  }

  const int fd = request->getFd();
  io_uring_sqe* sqe = getSqe(&ring_);
  if (sqe == nullptr) {
    LOG << "io_uring SQ full, cannot add poll for fd=" << fd;
    return;
  }

  // poll 是一次性操作：它完成后必须由 Channel 回调重新设置兴趣事件，
  // 再提交新的 poll。token 而非 fd 会作为 CQE 的唯一身份。
  const std::uint64_t token = operations_.track(UringOp::kPoll, fd, request);
  operations_.bindPoll(fd, token);
  io_uring_prep_poll_add(sqe, fd, epollToPollEvents(request->getEvents()));
  io_uring_sqe_set_data64(sqe, token);
}

void IoUring::cancelPoll(int fd) {
  const auto token = operations_.pollToken(fd);
  if (!token.has_value()) {
    return;
  }

  io_uring_sqe* sqe = getSqe(&ring_);
  if (sqe == nullptr) {
    LOG << "io_uring SQ full, cannot cancel poll for fd=" << fd;
    return;
  }

  io_uring_prep_poll_remove(sqe, *token);
  // Cancellation CQEs have no user callback. The target operation's CQE is
  // still consumed through its own token and releases the old Channel safely.
  io_uring_sqe_set_data64(sqe, 0);
}

void IoUring::cancelOperation(UringOp type, int fd) {
  const auto token = operations_.operationToken(type, fd);
  if (!token.has_value()) {
    return;
  }

  io_uring_sqe* sqe = getSqe(&ring_);
  if (sqe == nullptr) {
    LOG << "io_uring SQ full, cannot cancel operation for fd=" << fd;
    return;
  }

  io_uring_prep_cancel64(sqe, *token, 0);
  io_uring_sqe_set_data64(sqe, 0);
}

void IoUring::epoll_add(SP_Channel request, int timeout) {
  if (!request || request->getFd() < 0) {
    return;
  }

  const int fd = request->getFd();
  request->activate();
  request->EqualAndUpdateLastEvents();
  channels_[fd] = request;
  if (const std::shared_ptr<HttpData> holder = request->getHolder()) {
    // Channel 的 holder 是 weak_ptr；注册期间 Poller 需要持有连接，
    // 否则事件循环返回后 HttpData 可能提前析构。
    connectionOwners_[request.get()] = holder;
  }

  if (timeout > 0) {
    add_timer(request, timeout);
  }
  submitPollAdd(request);
}

void IoUring::epoll_mod(SP_Channel request, int timeout) {
  if (!request || !request->isActive()) {
    return;
  }

  if (timeout > 0) {
    add_timer(request, timeout);
  }
  if (request->EqualAndUpdateLastEvents()) {
    return;
  }

  // io_uring 的 poll 不能原地修改。取消旧请求并登记新请求；旧 CQE
  // 即使晚到，也会根据旧 token 被安全消费，不会影响新 poll。
  cancelPoll(request->getFd());
  submitPollAdd(request);
}

void IoUring::epoll_del(SP_Channel request) {
  if (!request) {
    return;
  }

  const int fd = request->getFd();
  // 先失活再取消：已经在内核中的 CQE 仍可能返回，但不会再驱动回调。
  request->deactivate();
  cancelPoll(fd);
  cancelOperation(UringOp::kRead, fd);
  cancelOperation(UringOp::kWrite, fd);
  connectionOwners_.erase(request.get());

  const auto channelIt = channels_.find(fd);
  if (channelIt != channels_.end() && channelIt->second == request) {
    channels_.erase(channelIt);
  }
}

void IoUring::submitRead(SP_Channel request, void* buffer, size_t length) {
  if (!request || !request->isActive() || buffer == nullptr || length == 0) {
    return;
  }

  io_uring_sqe* sqe = getSqe(&ring_);
  if (sqe == nullptr) {
    LOG << "io_uring SQ full, cannot submit read for fd=" << request->getFd();
    return;
  }

  // buffer 由 HttpData 的输入缓冲区提供，并在 isReading_ 为真期间保持有效。
  const std::uint64_t token =
      operations_.track(UringOp::kRead, request->getFd(), request);
  io_uring_prep_read(sqe, request->getFd(), buffer, length, 0);
  io_uring_sqe_set_data64(sqe, token);
}

void IoUring::submitWrite(SP_Channel request, const void* buffer, size_t length) {
  if (!request || !request->isActive() || buffer == nullptr || length == 0) {
    return;
  }

  io_uring_sqe* sqe = getSqe(&ring_);
  if (sqe == nullptr) {
    LOG << "io_uring SQ full, cannot submit write for fd=" << request->getFd();
    return;
  }

  // buffer 指向 HttpData 尚未 retrieve 的输出数据；完成前不能移动读指针。
  const std::uint64_t token =
      operations_.track(UringOp::kWrite, request->getFd(), request);
  io_uring_prep_write(sqe, request->getFd(), buffer, length, 0);
  io_uring_sqe_set_data64(sqe, token);
}

void IoUring::processEvents() {
  while (true) {
    // 一次提交本轮累积的 poll/read/write SQE。这样多个连接可批量进入内核。
    const int submitResult = io_uring_submit(&ring_);
    if (submitResult < 0) {
      LOG << "io_uring_submit failed: " << std::strerror(-submitResult);
      return;
    }

    __kernel_timespec timeout{};
    timeout.tv_sec = 10;

    io_uring_cqe* cqe = nullptr;
    const int waitResult = io_uring_wait_cqe_timeout(&ring_, &cqe, &timeout);
    if (waitResult < 0) {
      if (waitResult == -EINTR) {
        continue;
      }
      if (waitResult == -ETIME) {
        return;
      }
      LOG << "io_uring_wait_cqe_timeout failed: " << std::strerror(-waitResult);
      return;
    }

    // 已经等到至少一个 CQE 后，把当前可见的 CQE 一次取出，减少
    // EventLoop 在高并发下反复进入内核的次数。
    std::vector<io_uring_cqe*> cqes(queueSize_);
    const unsigned count = io_uring_peek_batch_cqe(&ring_, cqes.data(), queueSize_);
    for (unsigned index = 0; index < count; ++index) {
      const std::uint64_t token = io_uring_cqe_get_data64(cqes[index]);
      const int result = cqes[index]->res;
      if (token == 0) {
        continue;
      }

      // take() 会移除登记记录，并把原始 Channel 的 shared_ptr 交给本轮
      // 处理。后续即使 fd 已被复用，也不会按“当前 fd”错误路由。
      const auto operation = operations_.take(token);
      if (!operation.has_value()) {
        continue;
      }

      const SP_Channel& channel = operation->channel;
      // 关闭流程会先让 Channel 失活。旧 read/write/poll CQE 到达时只释放
      // 操作记录，不再进入 HttpData 回调。
      if (!channel || !channel->isActive() ||
          channel->getFd() != operation->fd) {
        continue;
      }

      if (operation->type == UringOp::kPoll) {
        // 当前 poll 已完成，允许随后 updatePoller() 为该 Channel 重新注册。
        channel->markPollConsumed();
        if (result == -ECANCELED) {
          continue;
        }
        if (result < 0) {
          LOG << "poll completion failed for fd=" << operation->fd << ": "
              << std::strerror(-result);
          continue;
        }
        channel->setRevents(pollToEpollEvents(static_cast<short>(result)));
        channel->setEvents(0);
        channel->handleEvents();
      } else if (operation->type == UringOp::kRead) {
        channel->handleReadComplete(result);
      } else {
        channel->handleWriteComplete(result);
      }
    }

    // 所有 CQE 都处理完后才归还给内核。提前 advance 会使 cqes 数组中的
    // 指针失效。
    io_uring_cq_advance(&ring_, count);
    if (count > 0) {
      return;
    }
  }
}

void IoUring::add_timer(SP_Channel request, int timeout) {
  const std::shared_ptr<HttpData> holder = request->getHolder();
  if (holder) {
    timerManager_.addTimer(holder, timeout);
  } else {
    LOG << "timer add failed: Channel has no HttpData holder";
  }
}

void IoUring::handleExpired() { timerManager_.handleExpiredEvent(); }
