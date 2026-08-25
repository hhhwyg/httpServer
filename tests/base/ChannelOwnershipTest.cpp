#include <cassert>
#include <memory>

#include "httpserver/transport/Channel.h"

int main() {
  auto channel = std::make_shared<httpserver::Channel>(nullptr, 7);

  auto owner = std::make_shared<int>(42);
  std::weak_ptr<int> weakOwner = owner;
  channel->setOwner(owner);
  owner.reset();
  assert(!weakOwner.expired());
  channel->clearOwner();
  assert(weakOwner.expired());

  bool cancelled = false;
  channel->setTimerCancellation([&cancelled] { cancelled = true; });
  channel->clearTimer();
  assert(cancelled);
  return 0;
}
