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
    : loop_(loop), fd_(fd), events_(0), lastEvents_(0) {}

Channel::~Channel() {
  // loop_->poller_->epoll_del(fd, events_);
  // close(fd_);
}

int Channel::getFd() { return fd_; }
void Channel::setFd(int fd) { fd_ = fd; }

void Channel::handleRead() {
  if (readHandler_) {
    readHandler_();
  }
}

void Channel::handleWrite() {
  if (writeHandler_) {
    writeHandler_();
  }
}

void Channel::handleConn() {
  if (connHandler_) {
    connHandler_();
  }
}

void Channel::handleReadComplete(int res) {
  if (asyncReadHandler_) {
    asyncReadHandler_(res);
  }
}

void Channel::handleWriteComplete(int res) {
  if (asyncWriteHandler_) {
    asyncWriteHandler_(res);
  }
}
