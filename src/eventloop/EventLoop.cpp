#include "httpserver/transport/EventLoop.h"
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <iostream>
#include<thread>
#include <cassert>
#include <mutex>
#include "Util.h"
#include "base/CurrentThread.h"
#include "httpserver/base/Logging.h"

namespace httpserver {

__thread EventLoop* t_loopInThisThread = 0;

int createEventfd() {
  int evtfd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (evtfd < 0) {
    LOG << "Failed in eventfd";
    abort();
  }
  return evtfd;
}//创建了一个可以被用户态程序用作 等待/通知 机制的文件描述符。

EventLoop::EventLoop(PollerBackend backend, const PollerConfig& config)
    : looping_(false),
      poller_(CreatePoller(backend, config)),
      wakeupFd_(createEventfd()),
      quit_(false),
      eventHandling_(false),
      callingPendingFunctors_(false),
      threadId_(CurrentThread::tid()),
      pwakeupChannel_(new Channel(this, wakeupFd_)) {
  if (t_loopInThisThread) {
    // LOG << "Another EventLoop " << t_loopInThisThread << " exists in this
    // thread " << threadId_;
  } else {
    t_loopInThisThread = this;
  }
  // pwakeupChannel_->setEvents(EPOLLIN | EPOLLET | EPOLLONESHOT);
  pwakeupChannel_->setEvents(EPOLLIN | EPOLLET);
  pwakeupChannel_->setReadHandler(std::bind(&EventLoop::handleRead, this));
  pwakeupChannel_->setConnHandler(std::bind(&EventLoop::handleConn, this));
  poller_->epoll_add(pwakeupChannel_, 0);
}

void EventLoop::handleConn() {
  // poller_->epoll_mod(wakeupFd_, pwakeupChannel_, (EPOLLIN | EPOLLET |
  // EPOLLONESHOT), 0);
  updatePoller(pwakeupChannel_, 0);
}

EventLoop::~EventLoop() {
  // wakeupChannel_->disableAll();
  // wakeupChannel_->remove();
  close(wakeupFd_);
  t_loopInThisThread = NULL;
}

void EventLoop::wakeup() {
  uint64_t one = 1;
  ssize_t n = writen(wakeupFd_, (char*)(&one), sizeof one);
  if (n != sizeof one) {
    LOG << "EventLoop::wakeup() writes " << n << " bytes instead of 8";
  }
}

void EventLoop::handleRead() {
  uint64_t one = 1;
  ssize_t n = readn(wakeupFd_, &one, sizeof one);
  if (n != sizeof one) {
    LOG << "EventLoop::handleRead() reads " << n << " bytes instead of 8";
  }
  // pwakeupChannel_->setEvents(EPOLLIN | EPOLLET | EPOLLONESHOT);
  pwakeupChannel_->setEvents(EPOLLIN | EPOLLET);
}

void EventLoop::runInLoop(Functor&& cb) {
  if (isInLoopThread())
    cb();
  else
    queueInLoop(std::move(cb));
}

bool EventLoop::isInLoopThread() const {
  return threadId_ == CurrentThread::tid();
}

void EventLoop::assertInLoopThread() {
  assert(isInLoopThread());
}

void EventLoop::shutdown(ChannelPtr channel) {
  shutDownWR(channel->getFd());
}

void EventLoop::removeFromPoller(ChannelPtr channel) {
  poller_->epoll_del(channel);
}

void EventLoop::updatePoller(ChannelPtr channel, int timeout) {
  poller_->epoll_mod(channel, timeout);
}

void EventLoop::addToPoller(ChannelPtr channel, int timeout) {
  poller_->epoll_add(channel, timeout);
}

void EventLoop::submitRead(ChannelPtr channel, void* buffer, std::size_t len) {
  poller_->submitRead(channel, buffer, len);
}

void EventLoop::submitWrite(ChannelPtr channel,
                            const void* buffer,
                            std::size_t len) {
  poller_->submitWrite(channel, buffer, len);
}

void EventLoop::queueInLoop(Functor&& cb) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    pendingFunctors_.emplace_back(std::move(cb));
  }

  if (!isInLoopThread() || callingPendingFunctors_) wakeup();
}

void EventLoop::loop() {
  //std::cout << "EventLoop running in thread " << std::this_thread::get_id() << std::endl;
  assert(!looping_);
  assert(isInLoopThread());
  looping_ = true;
  quit_ = false;
  // LOG_TRACE << "EventLoop " << this << " start looping";
  while (!quit_) {
    // cout << "doing" << endl;
    eventHandling_ = true;
    poller_->processEvents();
    eventHandling_ = false;
    doPendingFunctors();
    poller_->handleExpired();
  }
  looping_ = false;
}

void EventLoop::doPendingFunctors() {
  std::vector<Functor> functors;
  callingPendingFunctors_ = true;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    functors.swap(pendingFunctors_);
  }

  for (std::size_t i = 0; i < functors.size(); ++i) functors[i]();
  callingPendingFunctors_ = false;
}

void EventLoop::quit() {
  quit_ = true;
  if (!isInLoopThread()) {
    wakeup();
  }
}

}  // namespace httpserver
