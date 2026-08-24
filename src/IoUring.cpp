#include "IoUring.h"

#include <cerrno>
#include <cstring>
#include <poll.h>
#include <sys/epoll.h>

#include "base/Logging.h"

namespace {

short epollToPollEvents(__uint32_t epollEvents) {
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

IoUring::IoUring() {
  const int result = io_uring_queue_init(kRingSize, &ring_, 0);
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

void IoUring::epoll_add(SP_Channel request, int timeout) {
  if (!request || request->getFd() < 0) {
    return;
  }

  const int fd = request->getFd();
  request->activate();
  request->EqualAndUpdateLastEvents();
  channels_[fd] = request;
  if (const std::shared_ptr<HttpData> holder = request->getHolder()) {
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

  cancelPoll(request->getFd());
  submitPollAdd(request);
}

void IoUring::epoll_del(SP_Channel request) {
  if (!request) {
    return;
  }

  const int fd = request->getFd();
  request->deactivate();
  cancelPoll(fd);
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

  const std::uint64_t token =
      operations_.track(UringOp::kWrite, request->getFd(), request);
  io_uring_prep_write(sqe, request->getFd(), buffer, length, 0);
  io_uring_sqe_set_data64(sqe, token);
}

void IoUring::processEvents() {
  while (true) {
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

    io_uring_cqe* cqes[kRingSize];
    const unsigned count = io_uring_peek_batch_cqe(&ring_, cqes, kRingSize);
    for (unsigned index = 0; index < count; ++index) {
      const std::uint64_t token = io_uring_cqe_get_data64(cqes[index]);
      const int result = cqes[index]->res;
      if (token == 0) {
        continue;
      }

      const auto operation = operations_.take(token);
      if (!operation.has_value()) {
        continue;
      }

      const SP_Channel& channel = operation->channel;
      if (!channel || !channel->isActive() ||
          channel->getFd() != operation->fd) {
        continue;
      }

      if (operation->type == UringOp::kPoll) {
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
