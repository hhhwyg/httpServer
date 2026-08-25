#pragma once
#include <sys/epoll.h>
#include <memory>
#include <unordered_map>
#include <vector>
#include "Timer.h"
#include "httpserver/transport/Poller.h"


class Epoll : public httpserver::Poller {
 public:
  explicit Epoll(const httpserver::PollerConfig& config = {});
  ~Epoll() override;
  void add(httpserver::ChannelPtr channel, int timeout) override;
  void modify(httpserver::ChannelPtr channel, int timeout) override;
  void remove(httpserver::ChannelPtr channel) override;
  void submitRead(httpserver::ChannelPtr request, void* buffer,
                  std::size_t length) override;
  void submitWrite(httpserver::ChannelPtr request, const void* buffer,
                   std::size_t length) override;
  void processEvents() override;
  std::vector<httpserver::ChannelPtr> poll();
  std::vector<httpserver::ChannelPtr> getEventsRequest(int events_num);
  void addTimer(httpserver::ChannelPtr channel, int timeout);
  int getEpollFd() { return epollFd_; }
  void handleExpired() override;
  const char* name() const override { return "epoll"; }

 private:
  int epollFd_;
  std::vector<epoll_event> events_;
  std::unordered_map<httpserver::Channel*, httpserver::ChannelPtr> channels_;
  std::unordered_map<httpserver::Channel*, std::shared_ptr<void>>
      connectionOwners_;
  TimerManager timerManager_;
};
