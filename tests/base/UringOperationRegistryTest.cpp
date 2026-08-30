#include <cassert>

#include "IoUringOperation.h"

int main() {
  UringOperationRegistry registry;

  auto oldChannel = std::make_shared<httpserver::Channel>(nullptr, 42);
  auto replacementChannel = std::make_shared<httpserver::Channel>(nullptr, 42);
  const std::uint64_t oldPoll = registry.track(UringOp::kPoll, 42, oldChannel);
  registry.bindPoll(42, oldPoll);

  // A replacement poll can be registered before the cancellation CQE for the
  // old poll arrives. Consuming that stale CQE must not erase the new token.
  const std::uint64_t newPoll =
      registry.track(UringOp::kPoll, 42, replacementChannel);
  registry.bindPoll(42, newPoll);

  const auto staleOperation = registry.take(oldPoll);
  assert(staleOperation.has_value());
  assert(staleOperation->token == oldPoll);
  assert(staleOperation->fd == 42);
  assert(staleOperation->channel == oldChannel);
  assert(staleOperation->channel != replacementChannel);
  assert(registry.pollToken(42).has_value());
  assert(*registry.pollToken(42) == newPoll);

  const auto currentOperation = registry.take(newPoll);
  assert(currentOperation.has_value());
  assert(!registry.pollToken(42).has_value());

  const std::uint64_t read = registry.track(UringOp::kRead, 7, nullptr);
  const auto readOperation = registry.take(read);
  assert(readOperation.has_value());
  assert(readOperation->type == UringOp::kRead);
  assert(registry.size() == 0);
  return 0;
}
