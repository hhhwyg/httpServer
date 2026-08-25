#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>

class Channel;

enum class UringOp {
  kPoll,
  kRead,
  kWrite,
};

struct UringOperation {
  std::uint64_t token;
  UringOp type;
  int fd;
  std::shared_ptr<Channel> channel;
};

// user_data is an operation token, never a file descriptor. A completion
// therefore remains associated with the Channel that submitted it even when
// the kernel reuses the numeric fd for a different connection.
class UringOperationRegistry {
 public:
  std::uint64_t track(UringOp type, int fd, std::shared_ptr<Channel> channel) {
    const std::uint64_t token = nextToken();
    operations_.emplace(token, UringOperation{token, type, fd, std::move(channel)});
    return token;
  }

  void bindPoll(int fd, std::uint64_t token) { pollTokens_[fd] = token; }

  std::optional<std::uint64_t> pollToken(int fd) const {
    const auto it = pollTokens_.find(fd);
    if (it == pollTokens_.end()) {
      return std::nullopt;
    }
    return it->second;
  }

  std::optional<std::uint64_t> operationToken(UringOp type, int fd) const {
    for (const auto& [token, operation] : operations_) {
      if (operation.type == type && operation.fd == fd) {
        return token;
      }
    }
    return std::nullopt;
  }

  std::optional<UringOperation> take(std::uint64_t token) {
    const auto it = operations_.find(token);
    if (it == operations_.end()) {
      return std::nullopt;
    }

    UringOperation operation = std::move(it->second);
    operations_.erase(it);

    if (operation.type == UringOp::kPoll) {
      const auto pollIt = pollTokens_.find(operation.fd);
      if (pollIt != pollTokens_.end() && pollIt->second == token) {
        pollTokens_.erase(pollIt);
      }
    }
    return operation;
  }

  std::size_t size() const { return operations_.size(); }

 private:
  std::uint64_t nextToken() {
    // Token zero is reserved for internal cancellation SQEs.
    do {
      ++nextToken_;
    } while (nextToken_ == 0 || operations_.count(nextToken_) != 0);
    return nextToken_;
  }

  std::uint64_t nextToken_ = 0;
  std::unordered_map<std::uint64_t, UringOperation> operations_;
  std::unordered_map<int, std::uint64_t> pollTokens_;
};
