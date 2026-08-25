#include "httpserver/transport/Poller.h"

#include "Epoll.h"
#include "IoUring.h"

namespace httpserver {

std::unique_ptr<Poller> CreatePoller(PollerBackend backend,
                                     const PollerConfig& config) {
  switch (backend) {
    case PollerBackend::kEpoll:
      return std::make_unique<Epoll>(config);
    case PollerBackend::kIoUring:
      return std::make_unique<IoUring>(config);
  }
  return nullptr;
}

std::optional<PollerBackend> ParsePollerBackend(std::string_view value) {
  if (value == "uring" || value == "io_uring") {
    return PollerBackend::kIoUring;
  }
  if (value == "epoll") {
    return PollerBackend::kEpoll;
  }
  return std::nullopt;
}

const char* PollerBackendName(PollerBackend backend) {
  switch (backend) {
    case PollerBackend::kIoUring:
      return "io_uring";
    case PollerBackend::kEpoll:
      return "epoll";
  }
  return "unknown";
}

}  // namespace httpserver
