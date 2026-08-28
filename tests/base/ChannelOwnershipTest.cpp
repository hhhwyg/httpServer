#include <cassert>
#include <memory>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "httpserver/transport/Channel.h"
#include "httpserver/transport/Poller.h"

int main() {
  int sockets[2]{};
  assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);

  auto poller = httpserver::CreatePoller(httpserver::PollerBackend::kEpoll);
  auto channel = std::make_shared<httpserver::Channel>(nullptr, sockets[0]);
  channel->setEvents(EPOLLIN);

  auto owner = std::make_shared<int>(42);
  std::weak_ptr<int> weakOwner = owner;
  channel->setOwner(owner);
  poller->add(channel, 0);
  owner.reset();
  assert(!weakOwner.expired());

  poller->remove(channel);
  assert(weakOwner.expired());

  bool cancelled = false;
  channel->setTimerCancellation([&cancelled] { cancelled = true; });
  channel->clearTimer();
  assert(cancelled);

  close(sockets[1]);
  return 0;
}
