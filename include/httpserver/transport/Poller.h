#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string_view>

#include "httpserver/transport/Channel.h"

namespace httpserver {

struct PollerConfig {
  int maxFds = 100000;
  unsigned ioUringQueueSize = 4096;
};

enum class PollerBackend {
  kIoUring,
  kEpoll,
};

class Poller {
 public:
  virtual ~Poller() = default;

  virtual void epoll_add(ChannelPtr request, int timeout) = 0;
  virtual void epoll_mod(ChannelPtr request, int timeout) = 0;
  virtual void epoll_del(ChannelPtr request) = 0;
  virtual void submitRead(ChannelPtr request, void* buffer,
                          std::size_t length) = 0;
  virtual void submitWrite(ChannelPtr request, const void* buffer,
                           std::size_t length) = 0;
  virtual void processEvents() = 0;
  virtual void handleExpired() = 0;
  virtual const char* name() const = 0;
};

std::unique_ptr<Poller> CreatePoller(
    PollerBackend backend, const PollerConfig& config = {});
std::optional<PollerBackend> ParsePollerBackend(std::string_view value);
const char* PollerBackendName(PollerBackend backend);

}  // namespace httpserver
