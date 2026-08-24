#pragma once
#include <sys/epoll.h>
#include <memory>
#include <unordered_map>
#include <vector>
#include "Poller.h"
#include "HttpData.h"
#include "Timer.h"


class Epoll : public Poller {
 public:
  Epoll();
  ~Epoll() override;
  void epoll_add(SP_Channel request, int timeout) override;
  void epoll_mod(SP_Channel request, int timeout) override;
  void epoll_del(SP_Channel request) override;
  void submitRead(SP_Channel request, void* buffer, std::size_t length) override;
  void submitWrite(SP_Channel request, const void* buffer,
                   std::size_t length) override;
  void processEvents() override;
  std::vector<std::shared_ptr<Channel>> poll();
  std::vector<std::shared_ptr<Channel>> getEventsRequest(int events_num);
  void add_timer(std::shared_ptr<Channel> request_data, int timeout);
  int getEpollFd() { return epollFd_; }
  void handleExpired() override;
  const char* name() const override { return "epoll"; }

 private:
  int epollFd_;
  std::vector<epoll_event> events_;
  std::unordered_map<Channel*, SP_Channel> channels_;
  std::unordered_map<Channel*, std::shared_ptr<HttpData>> connectionOwners_;
  TimerManager timerManager_;
};
