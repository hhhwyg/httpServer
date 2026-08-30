#include "httpserver/application/DatabaseExecutor.h"

#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace httpserver {
namespace {

class ExecutorState {
 public:
  ExecutorState() {
    workers_.emplace_back(&ExecutorState::run, this);
    workers_.emplace_back(&ExecutorState::run, this);
  }

  ~ExecutorState() { shutdown(); }

  bool submit(std::function<void()> task) {
    if (!task) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    // 队列满时立即拒绝，避免数据库变慢时无限占用内存。
    if (stopping_ || tasks_.size() >= 256) return false;
    tasks_.push(std::move(task));
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

 private:
  void run() {
    for (;;) {
      std::function<void()> task;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        available_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });
        if (stopping_ && tasks_.empty()) return;
        task = std::move(tasks_.front());
        tasks_.pop();
      }
      try {
        task();
      } catch (...) {
        // 任务异常不能杀死工作线程；控制器负责转换为稳定的 HTTP 响应。
      }
    }
  }

  std::mutex mutex_;
  std::condition_variable available_;
  std::queue<std::function<void()>> tasks_;
  std::vector<std::thread> workers_;
  bool stopping_ = false;
};

ExecutorState*& state() {
  static ExecutorState* instance = nullptr;
  return instance;
}

std::mutex& stateMutex() {
  static std::mutex mutex;
  return mutex;
}

}  // namespace

bool DatabaseExecutor::Submit(std::function<void()> task) {
  // 用独立锁保护延迟创建，避免多个 EventLoop 同时首次提交时重复建线程。
  std::lock_guard<std::mutex> lock(stateMutex());
  if (state() == nullptr) state() = new ExecutorState();
  return state()->submit(std::move(task));
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
