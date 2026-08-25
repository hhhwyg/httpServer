#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include <sys/epoll.h>

namespace httpserver {

class EventLoop;

class Channel {
 private:
  using Callback = std::function<void()>;
  using AsyncCallback = std::function<void(int)>;

  EventLoop* loop_;
  int fd_;
  std::uint32_t events_;
  std::uint32_t revents_;
  std::uint32_t lastEvents_;
  bool active_;
  std::weak_ptr<void> owner_;
  Callback timeoutHandler_;

  Callback readHandler_;
  Callback writeHandler_;
  Callback errorHandler_;
  Callback connHandler_;
  Callback timerCancellation_;
  AsyncCallback asyncReadHandler_;
  AsyncCallback asyncWriteHandler_;

 public:
  Channel(EventLoop* loop);
  Channel(EventLoop* loop, int fd);
  ~Channel();

  int getFd();
  void setFd(int fd);

  void setOwner(std::shared_ptr<void> owner) { owner_ = std::move(owner); }
  void clearOwner() { owner_.reset(); }
  std::shared_ptr<void> lockOwner() const { return owner_.lock(); }

  void setTimeoutHandler(Callback handler) { timeoutHandler_ = std::move(handler); }
  Callback getTimeoutHandler() const { return timeoutHandler_; }
  void setTimerCancellation(Callback cancellation) {
    timerCancellation_ = std::move(cancellation);
  }
  void clearTimer() {
    if (timerCancellation_) timerCancellation_();
    timerCancellation_ = {};
  }

  void setReadHandler(Callback&& handler) { readHandler_ = std::move(handler); }
  void setWriteHandler(Callback&& handler) {
    writeHandler_ = std::move(handler);
  }
  void setErrorHandler(Callback&& handler) {
    errorHandler_ = std::move(handler);
  }
  void setConnHandler(Callback&& handler) { connHandler_ = std::move(handler); }
  void setAsyncReadHandler(AsyncCallback&& handler) {
    asyncReadHandler_ = std::move(handler);
  }
  void setAsyncWriteHandler(AsyncCallback&& handler) {
    asyncWriteHandler_ = std::move(handler);
  }

  void handleEvents() {
    events_ = 0;
    if ((revents_ & EPOLLHUP) && !(revents_ & EPOLLIN)) {
      events_ = 0;
      return;
    }
    if (revents_ & EPOLLERR) {
      if (errorHandler_) errorHandler_();
      events_ = 0;
      return;
    }
    if (revents_ & (EPOLLIN | EPOLLPRI | EPOLLRDHUP)) handleRead();
    if (revents_ & EPOLLOUT) handleWrite();
    handleConn();
  }

  void handleRead();
  void handleWrite();
  void handleReadComplete(int result);
  void handleWriteComplete(int result);
  void handleError(int fd, int errorNumber, std::string shortMessage);
  void handleConn();
  void update();

  void setRevents(std::uint32_t events) { revents_ = events; }
  void activate() { active_ = true; }
  void deactivate() {
    active_ = false;
    events_ = 0;
    revents_ = 0;
    lastEvents_ = 0;
  }
  bool isActive() const { return active_; }
  void markPollConsumed() { lastEvents_ = 0; }

  void setEvents(std::uint32_t events) { events_ = events; }
  std::uint32_t& getEvents() { return events_; }

  bool EqualAndUpdateLastEvents() {
    const bool equal = lastEvents_ == events_;
    lastEvents_ = events_;
    return equal;
  }

  std::uint32_t getLastEvents() { return lastEvents_; }
};

using ChannelPtr = std::shared_ptr<Channel>;

}  // namespace httpserver
