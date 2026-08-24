#include "Epoll.h"
#include <assert.h>
#include <errno.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <deque>
#include <queue>
#include "Util.h"
#include "base/Logging.h"


#include <arpa/inet.h>
#include <iostream>
using namespace std;

const int EVENTSNUM = 4096;
const int EPOLLWAIT_TIME = 10000;

typedef shared_ptr<Channel> SP_Channel;

Epoll::Epoll() : epollFd_(epoll_create1(EPOLL_CLOEXEC)), events_(EVENTSNUM) {
  assert(epollFd_ > 0);
}
Epoll::~Epoll() {}

// 注册新描述符
void Epoll::epoll_add(SP_Channel request, int timeout) {
  int fd = request->getFd();
  if (timeout > 0) {
    add_timer(request, timeout);
    fd2http_[fd] = request->getHolder();
  }
  struct epoll_event event;
  memset(&event, 0, sizeof(event));
  event.data.fd=fd;
  event.events = request->getEvents();
  request->EqualAndUpdateLastEvents();

  fd2chan_[fd] = request;
  if (epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &event) < 0) {
    perror("epoll_add error");
    fd2chan_[fd].reset();
  }
  //std::cout << "[epoll_add] fd=" << fd << " events=" << request->getEvents() << std::endl;
}

// 修改描述符状态
void Epoll::epoll_mod(SP_Channel request, int timeout) {
  if (timeout > 0) add_timer(request, timeout);
  int fd = request->getFd();
  if (!request->EqualAndUpdateLastEvents()) {
    struct epoll_event event;
    memset(&event, 0, sizeof(event));
    event.data.fd = fd;
    event.events = request->getEvents();
    if (epoll_ctl(epollFd_, EPOLL_CTL_MOD, fd, &event) < 0) {
      perror("epoll_mod error");
      fd2chan_[fd].reset();
    }
  }
}


// 从epoll中删除描述符
void Epoll::epoll_del(SP_Channel request) {
  //std::cout<< "epoll_del fd=" <<request->getFd()<<std::endl;

  int fd = request->getFd();
  struct epoll_event event;
  memset(&event, 0, sizeof(event));
  event.data.fd = fd;
 event.events = request->getLastEvents();
  // event.events = 0;
  // request->EqualAndUpdateLastEvents()
  if (epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, &event) < 0) {
    perror("epoll_del error");
  }
  fd2chan_[fd].reset();
  fd2http_[fd].reset();
}

// 返回活跃事件数
std::vector<SP_Channel> Epoll::poll() {
    while (true) {
        int event_count = epoll_wait(epollFd_, &*events_.begin(), 
                                     events_.size(), EPOLLWAIT_TIME);
        
        if (event_count < 0) {
            // 处理被信号中断的情况
            if (errno == EINTR) {
                continue; 
            }
            // 真正的错误才打印
            perror("epoll wait error");
            // 根据你的设计，这里通常应该返回空或跳出循环，避免死循环报错
            return std::vector<SP_Channel>(); 
        }
        
        // 只有 event_count >= 0 时才去处理事件
        std::vector<SP_Channel> req_data = getEventsRequest(event_count);
        
        // 如果有事件或者你想处理超时逻辑，这里可以返回
        if (req_data.size() > 0) {
            return req_data;
        }
        
        // 如果 event_count == 0 且没有定时任务，循环会继续进行下一次 epoll_wait
    }
}

void Epoll::handleExpired() { timerManager_.handleExpiredEvent(); }

// 分发处理函数
std::vector<SP_Channel> Epoll::getEventsRequest(int events_num) {
  std::vector<SP_Channel> req_data;
  for (int i = 0; i < events_num; ++i) {
    // 获取有事件产生的描述符
    int fd = events_[i].data.fd;
    SP_Channel cur_req = fd2chan_[fd];

    if (cur_req) {
      cur_req->setRevents(events_[i].events);
      cur_req->setEvents(0);
      // 加入线程池之前将Timer和request分离
      // cur_req->seperateTimer();
      req_data.push_back(cur_req);
    } else {
      LOG << "SP cur_req is invalid";
    }
  }
  return req_data;
}

void Epoll::add_timer(SP_Channel request_data, int timeout) {
  shared_ptr<HttpData> t = request_data->getHolder();
  if (t)
    timerManager_.addTimer(t, timeout);
  else
    LOG << "timer add fail";
}