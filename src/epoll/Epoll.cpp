#include "Epoll.h"

#include <cassert>
#include <cerrno>
#include <cstring>
#include <unistd.h>

#include "httpserver/base/Logging.h"

namespace {

constexpr int kEventsNum = 4096;
constexpr int kEpollWaitTimeMs = 10000;

}  // namespace

Epoll::Epoll(const httpserver::PollerConfig& config)
    : epollFd_(epoll_create1(EPOLL_CLOEXEC)), events_(kEventsNum) {
  (void)config;
  assert(epollFd_ >= 0);
}

Epoll::~Epoll() {
  if (epollFd_ >= 0) {
    close(epollFd_);
  }
}

void Epoll::epoll_add(httpserver::ChannelPtr request, int timeout) {
  if (!request || request->getFd() < 0) {
    return;
  }

  request->activate();
  request->EqualAndUpdateLastEvents();
  channels_[request.get()] = request;
  if (const std::shared_ptr<HttpData> holder = request->getHolder()) {
    connectionOwners_[request.get()] = holder;
  }
  if (timeout > 0) {
    add_timer(request, timeout);
  }

  epoll_event event{};
  event.data.ptr = request.get();
  event.events = request->getEvents();
  if (epoll_ctl(epollFd_, EPOLL_CTL_ADD, request->getFd(), &event) < 0) {
    LOG << "epoll add failed for fd=" << request->getFd() << ": "
        << std::strerror(errno);
    connectionOwners_.erase(request.get());
    channels_.erase(request.get());
    request->deactivate();
  }
}

void Epoll::epoll_mod(httpserver::ChannelPtr request, int timeout) {
  if (!request || !request->isActive()) {
    return;
  }
  if (timeout > 0) {
    add_timer(request, timeout);
  }
  if (request->EqualAndUpdateLastEvents()) {
    return;
  }

  epoll_event event{};
  event.data.ptr = request.get();
  event.events = request->getEvents();
  if (epoll_ctl(epollFd_, EPOLL_CTL_MOD, request->getFd(), &event) < 0) {
    LOG << "epoll mod failed for fd=" << request->getFd() << ": "
        << std::strerror(errno);
  }
}

void Epoll::epoll_del(httpserver::ChannelPtr request) {
  if (!request) {
    return;
  }

  const int fd = request->getFd();
  request->deactivate();
  connectionOwners_.erase(request.get());
  channels_.erase(request.get());
  if (fd >= 0 && epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr) < 0 &&
      errno != ENOENT && errno != EBADF) {
    LOG << "epoll del failed for fd=" << fd << ": " << std::strerror(errno);
  }
}

void Epoll::submitRead(httpserver::ChannelPtr request, void* buffer,
                       std::size_t length) {
  if (!request || !request->isActive() || !buffer || length == 0) {
    return;
  }

  const ssize_t result = read(request->getFd(), buffer, length);
  request->handleReadComplete(result < 0 ? -errno : static_cast<int>(result));
}

void Epoll::submitWrite(httpserver::ChannelPtr request, const void* buffer,
                        std::size_t length) {
  if (!request || !request->isActive() || !buffer || length == 0) {
    return;
  }

  const ssize_t result = write(request->getFd(), buffer, length);
  request->handleWriteComplete(result < 0 ? -errno : static_cast<int>(result));
}

void Epoll::processEvents() {
  const std::vector<httpserver::ChannelPtr> requests = poll();
  for (const httpserver::ChannelPtr& request : requests) {
    if (request && request->isActive()) {
      request->handleEvents();
    }
  }
}

std::vector<httpserver::ChannelPtr> Epoll::poll() {
  int eventCount = 0;
  do {
    eventCount = epoll_wait(epollFd_, events_.data(),
                            static_cast<int>(events_.size()),
                            kEpollWaitTimeMs);
  } while (eventCount < 0 && errno == EINTR);

  if (eventCount < 0) {
    LOG << "epoll wait failed: " << std::strerror(errno);
    return {};
  }
  return getEventsRequest(eventCount);
}

std::vector<httpserver::ChannelPtr> Epoll::getEventsRequest(int eventsNum) {
  std::vector<httpserver::ChannelPtr> requests;
  requests.reserve(eventsNum);
  for (int index = 0; index < eventsNum; ++index) {
    // A stale event is looked up but never dereferenced before the lookup,
    // so it cannot be redirected after the kernel reuses a numeric fd.
    auto* const rawChannel =
        static_cast<httpserver::Channel*>(events_[index].data.ptr);
    const auto channelIt = channels_.find(rawChannel);
    if (channelIt == channels_.end()) {
      continue;
    }

    const httpserver::ChannelPtr& request = channelIt->second;
    if (!request || !request->isActive()) {
      continue;
    }
    request->setRevents(events_[index].events);
    request->setEvents(0);
    requests.push_back(request);
  }
  return requests;
}

void Epoll::add_timer(httpserver::ChannelPtr requestData, int timeout) {
  const std::shared_ptr<HttpData> holder = requestData->getHolder();
  if (holder) {
    timerManager_.addTimer(holder, timeout);
  } else {
    LOG << "timer add failed: Channel has no HttpData holder";
  }
}

void Epoll::handleExpired() { timerManager_.handleExpiredEvent(); }
