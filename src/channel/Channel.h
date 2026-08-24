#pragma once
#include <sys/epoll.h>
#include <sys/epoll.h>
#include <iostream>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include "Timer.h"

class EventLoop;
class HttpData;

class Channel {
 private:
  typedef std::function<void()> CallBack;
  EventLoop *loop_;
  int fd_;
  __uint32_t events_;//关注的事
  __uint32_t revents_;//实际发生的事
  __uint32_t lastEvents_;//上一个同步给内核关注的事件
  bool active_;

  // 方便找到上层持有该Channel的对象
  std::weak_ptr<HttpData> holder_;

 private:
  int parse_URI();
  int parse_Headers();
  int analysisRequest();

  CallBack readHandler_;
  CallBack writeHandler_;
  CallBack errorHandler_;
  CallBack connHandler_;
  
  typedef std::function<void(int)> AsyncReadCallBack;
  typedef std::function<void(int)> AsyncWriteCallBack;
  AsyncReadCallBack asyncReadHandler_;
  AsyncWriteCallBack asyncWriteHandler_;

 public:
  Channel(EventLoop *loop);
  Channel(EventLoop *loop, int fd);
  ~Channel();
  int getFd();
  void setFd(int fd);

  void setHolder(std::shared_ptr<HttpData> holder) { holder_ = holder; }
  void clearHolder() { holder_.reset(); }
  std::shared_ptr<HttpData> getHolder() {
    std::shared_ptr<HttpData> ret(holder_.lock());
    return ret;
  }

  void setReadHandler(CallBack &&readHandler) { readHandler_ = readHandler; }
  void setWriteHandler(CallBack &&writeHandler) {
    writeHandler_ = writeHandler;
  }
  void setErrorHandler(CallBack &&errorHandler) {
    errorHandler_ = errorHandler;
  }
  void setConnHandler(CallBack &&connHandler) { connHandler_ = connHandler; }
  
  void setAsyncReadHandler(AsyncReadCallBack &&handler) { asyncReadHandler_ = handler; }
  void setAsyncWriteHandler(AsyncWriteCallBack &&handler) { asyncWriteHandler_ = handler; }

  //处理事件
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
    if (revents_ & (EPOLLIN | EPOLLPRI | EPOLLRDHUP)) {
      handleRead();
    }
    if (revents_ & EPOLLOUT) {
      handleWrite();
    }
    handleConn();
  }

  
  void handleRead();
  void handleWrite();
  void handleReadComplete(int res);
  void handleWriteComplete(int res);
  void handleError(int fd, int err_num, std::string short_msg);
  void handleConn();
  void update();

  void setRevents(__uint32_t ev) { revents_ = ev; }

  void activate() { active_ = true; }
  void deactivate() {
    active_ = false;
    events_ = 0;
    revents_ = 0;
    lastEvents_ = 0;
  }
  bool isActive() const { return active_; }
  void markPollConsumed() { lastEvents_ = 0; }

  void setEvents(__uint32_t ev) { events_ = ev; }
  __uint32_t &getEvents() { return events_; }

  bool EqualAndUpdateLastEvents() {
    bool ret = (lastEvents_ == events_);
    lastEvents_ = events_;
    return ret;
  }

  __uint32_t getLastEvents() { return lastEvents_; }
};

typedef std::shared_ptr<Channel> SP_Channel;
