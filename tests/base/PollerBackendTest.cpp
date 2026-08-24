#include <cassert>
#include <cstring>

#include "Poller.h"

int main() {
  const auto uring = ParsePollerBackend("uring");
  assert(uring.has_value());
  assert(*uring == PollerBackend::kIoUring);
  assert(std::strcmp(PollerBackendName(*uring), "io_uring") == 0);

  const auto epoll = ParsePollerBackend("epoll");
  assert(epoll.has_value());
  assert(*epoll == PollerBackend::kEpoll);
  assert(std::strcmp(PollerBackendName(*epoll), "epoll") == 0);

  assert(ParsePollerBackend("select").has_value() == false);
  assert(ParsePollerBackend("IO_URING").has_value() == false);
  return 0;
}
