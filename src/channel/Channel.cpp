#include "Channel.h"

#include <unistd.h>
#include <cstdlib>

#include <queue>

#include "IoUring.h"
#include "EventLoop.h"
#include "Util.h"

using namespace std;

//Channel::Channel(EventLoop *loop)
  //  : loop_(loop), events_(0), lastEvents_(0), fd_(0) {}

Channel::Channel(EventLoop *loop, int fd)
    : loop_(loop), fd_(fd), events_(0), revents_(0), lastEvents_(0), active_(false) {}

Channel::~Channel() {
  // loop_->poller_->epoll_del(fd, events_);
  // close(fd_);
}

int Channel::getFd() { return fd_; }
void Channel::setFd(int fd) { fd_ = fd; }

void Channel::handleRead() {
  if (active_ && readHandler_) {
    readHandler_();
  }
}

void Channel::handleWrite() {
  if (active_ && writeHandler_) {
    writeHandler_();
  }
}

void Channel::handleConn() {
  if (active_ && connHandler_) {
    connHandler_();
  }
}

void Channel::handleReadComplete(int res) {
  if (active_ && asyncReadHandler_) {
    asyncReadHandler_(res);
  }
}

void Channel::handleWriteComplete(int res) {
  if (active_ && asyncWriteHandler_) {
    asyncWriteHandler_(res);
  }
}
