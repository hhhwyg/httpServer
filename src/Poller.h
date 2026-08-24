#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string_view>

#include "Channel.h"

enum class PollerBackend {
  kIoUring,
  kEpoll,
};

// Poller owns the kernel-facing event mechanism. EventLoop and the protocol
// layer only depend on this contract, so their behavior remains identical when
// comparing epoll with io_uring.
class Poller {
 public:
  virtual ~Poller() = default;

  virtual void epoll_add(SP_Channel request, int timeout) = 0;
  virtual void epoll_mod(SP_Channel request, int timeout) = 0;
  virtual void epoll_del(SP_Channel request) = 0;
  virtual void submitRead(SP_Channel request, void* buffer, std::size_t length) = 0;
  virtual void submitWrite(SP_Channel request, const void* buffer,
                           std::size_t length) = 0;
  virtual void processEvents() = 0;
  virtual void handleExpired() = 0;
  virtual const char* name() const = 0;
};

std::unique_ptr<Poller> CreatePoller(PollerBackend backend);
std::optional<PollerBackend> ParsePollerBackend(std::string_view value);
const char* PollerBackendName(PollerBackend backend);
