#include <cassert>
#include <cstring>

#include "httpserver/transport/Poller.h"

int main() {
  const auto uring = httpserver::ParsePollerBackend("uring");
  assert(uring.has_value());
  assert(*uring == httpserver::PollerBackend::kIoUring);
  assert(std::strcmp(httpserver::PollerBackendName(*uring), "io_uring") == 0);

  const auto epoll = httpserver::ParsePollerBackend("epoll");
  assert(epoll.has_value());
  assert(*epoll == httpserver::PollerBackend::kEpoll);
  assert(std::strcmp(httpserver::PollerBackendName(*epoll), "epoll") == 0);

  assert(httpserver::ParsePollerBackend("select").has_value() == false);
  assert(httpserver::ParsePollerBackend("IO_URING").has_value() == false);
  return 0;
}
