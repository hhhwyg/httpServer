#include "httpserver/application/DatabaseExecutor.h"

#include <condition_variable>
#include <chrono>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace httpserver {
namespace {

class ExecutorState {
 public:
  explicit ExecutorState(DatabaseExecutor::Config config) : config_(config) {
    for (std::size_t index = 0; index < config_.workerCount; ++index) {
      workers_.emplace_back(&ExecutorState::run, this);
    }
  }

  ~ExecutorState() { shutdown(); }

  bool submit(std::function<void()> task,
              std::chrono::milliseconds queueDeadline,
              std::function<void()> onExpired) {
    if (!task) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    // 队列满时立即拒绝，避免数据库变慢时无限占用内存。
    if (stopping_ || tasks_.size() >= config_.queueCapacity) {
      ++rejected_;
      return false;
    }
    tasks_.push({std::move(task), std::move(onExpired),
                 std::chrono::steady_clock::now() + queueDeadline});
    available_.notify_one();
    return true;
  }

  void shutdown() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stopping_) return;
      // 不再接收新任务，但允许已入队任务在工作线程退出前完成。
      stopping_ = true;
    }
    available_.notify_all();
    for (std::thread& worker : workers_) {
      if (worker.joinable()) worker.join();
    }
  }

  DatabaseExecutor::Stats stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return {tasks_.size(), running_, rejected_, expired_};
  }

 private:
  void run() {
    for (;;) {
      Task task;
      std::function<void()> expiredHandler;
      bool expired = false;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        available_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });
        if (stopping_ && tasks_.empty()) return;
        task = std::move(tasks_.front());
        tasks_.pop();
        if (std::chrono::steady_clock::now() > task.deadline) {
          ++expired_;
          expired = true;
          expiredHandler = std::move(task.onExpired);
        } else {
          ++running_;
        }
      }
      if (expired) {
        if (expiredHandler) {
          try { expiredHandler(); } catch (...) {}
        }
        continue;
      }
      try {
        task.function();
      } catch (...) {
        // 任务异常不能杀死工作线程；控制器负责转换为稳定的 HTTP 响应。
      }
      {
        std::lock_guard<std::mutex> lock(mutex_);
        --running_;
      }
    }
  }

  struct Task {
    std::function<void()> function;
    std::function<void()> onExpired;
    std::chrono::steady_clock::time_point deadline;
  };

  const DatabaseExecutor::Config config_;
  mutable std::mutex mutex_;
  std::condition_variable available_;
  std::queue<Task> tasks_;
  std::vector<std::thread> workers_;
  std::size_t running_ = 0;
  std::size_t rejected_ = 0;
  std::size_t expired_ = 0;
  bool stopping_ = false;
};

ExecutorState*& state() {
  static ExecutorState* instance = nullptr;
  return instance;
}

DatabaseExecutor::Config& configuredState() {
  static DatabaseExecutor::Config config;
  return config;
}

std::mutex& stateMutex() {
  static std::mutex mutex;
  return mutex;
}

}  // namespace

bool DatabaseExecutor::Configure(Config config) {
  if (config.workerCount == 0 || config.queueCapacity == 0) return false;
  std::lock_guard<std::mutex> lock(stateMutex());
  if (state() != nullptr) return false;
  configuredState() = config;
  return true;
}

bool DatabaseExecutor::Submit(std::function<void()> task) {
  return Submit(std::move(task), std::chrono::seconds(3));
}

bool DatabaseExecutor::Submit(std::function<void()> task,
                              std::chrono::milliseconds queueDeadline) {
  return Submit(std::move(task), queueDeadline, {});
}

bool DatabaseExecutor::Submit(std::function<void()> task,
                              std::chrono::milliseconds queueDeadline,
                              std::function<void()> onExpired) {
  if (queueDeadline.count() <= 0) return false;
  // 用独立锁保护延迟创建，避免多个 EventLoop 同时首次提交时重复建线程。
  std::lock_guard<std::mutex> lock(stateMutex());
  if (state() == nullptr) state() = new ExecutorState(configuredState());
  return state()->submit(std::move(task), queueDeadline, std::move(onExpired));
}

DatabaseExecutor::Stats DatabaseExecutor::GetStats() {
  std::lock_guard<std::mutex> lock(stateMutex());
  return state() == nullptr ? Stats{} : state()->stats();
}

void DatabaseExecutor::Shutdown() {
  std::lock_guard<std::mutex> lock(stateMutex());
  if (state() != nullptr) {
    state()->shutdown();
    delete state();
    state() = nullptr;
  }
}

}  // namespace httpserver
