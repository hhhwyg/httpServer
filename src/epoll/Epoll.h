#pragma once
#include <sys/epoll.h>
#include <memory>
#include <unordered_map>
#include <vector>
#include "HttpData.h"
#include "Timer.h"
#include "httpserver/transport/Poller.h"


class Epoll : public httpserver::Poller {
 public:
  explicit Epoll(const httpserver::PollerConfig& config = {});
  ~Epoll() override;
  void epoll_add(httpserver::ChannelPtr request, int timeout) override;
  void epoll_mod(httpserver::ChannelPtr request, int timeout) override;
  void epoll_del(httpserver::ChannelPtr request) override;
  void submitRead(httpserver::ChannelPtr request, void* buffer,
                  std::size_t length) override;
  void submitWrite(httpserver::ChannelPtr request, const void* buffer,
                   std::size_t length) override;
  void processEvents() override;
  std::vector<httpserver::ChannelPtr> poll();
  std::vector<httpserver::ChannelPtr> getEventsRequest(int events_num);
  void add_timer(httpserver::ChannelPtr request_data, int timeout);
  int getEpollFd() { return epollFd_; }
  void handleExpired() override;
  const char* name() const override { return "epoll"; }

 private:
  int epollFd_;
  std::vector<epoll_event> events_;
  std::unordered_map<httpserver::Channel*, httpserver::ChannelPtr> channels_;
  std::unordered_map<httpserver::Channel*, std::shared_ptr<HttpData>>
      connectionOwners_;
  TimerManager timerManager_;
};
