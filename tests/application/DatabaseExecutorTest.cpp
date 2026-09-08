#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "httpserver/application/DatabaseExecutor.h"

int main() {
  assert(httpserver::DatabaseExecutor::Configure({2, 16}));
  std::atomic<int> completed{0};
  std::mutex mutex;
  std::condition_variable condition;

  for (int index = 0; index < 8; ++index) {
    assert(httpserver::DatabaseExecutor::Submit([&] {
      if (completed.fetch_add(1) + 1 == 8) condition.notify_one();
    }));
  }

  std::unique_lock<std::mutex> lock(mutex);
  assert(condition.wait_for(lock, std::chrono::seconds(2), [&] {
    return completed.load() == 8;
  }));
  httpserver::DatabaseExecutor::Shutdown();

  assert(httpserver::DatabaseExecutor::Configure({1, 1}));
  std::mutex gateMutex;
  std::condition_variable gate;
  bool running = false;
  bool release = false;
  assert(httpserver::DatabaseExecutor::Submit([&] {
    std::unique_lock<std::mutex> gateLock(gateMutex);
    running = true;
    gate.notify_one();
    gate.wait(gateLock, [&] { return release; });
  }));
  {
    std::unique_lock<std::mutex> gateLock(gateMutex);
    assert(gate.wait_for(gateLock, std::chrono::seconds(2), [&] { return running; }));
  }
  assert(httpserver::DatabaseExecutor::Submit([] {}));
  assert(!httpserver::DatabaseExecutor::Submit([] {}));
  const httpserver::DatabaseExecutor::Stats saturated =
      httpserver::DatabaseExecutor::GetStats();
  assert(saturated.queued == 1);
  assert(saturated.running == 1);
  assert(saturated.rejected == 1);
  {
    std::lock_guard<std::mutex> gateLock(gateMutex);
    release = true;
  }
  gate.notify_one();
  httpserver::DatabaseExecutor::Shutdown();

  assert(httpserver::DatabaseExecutor::Configure({1, 2}));
  std::atomic<bool> releaseDeadlineWorker{false};
  std::mutex deadlineMutex;
  std::condition_variable deadlineStarted;
  bool deadlineWorkerRunning = false;
  assert(httpserver::DatabaseExecutor::Submit([&] {
    {
      std::lock_guard<std::mutex> lock(deadlineMutex);
      deadlineWorkerRunning = true;
    }
    deadlineStarted.notify_one();
    while (!releaseDeadlineWorker.load()) std::this_thread::yield();
  }));
  {
    std::unique_lock<std::mutex> lock(deadlineMutex);
    assert(deadlineStarted.wait_for(lock, std::chrono::seconds(2), [&] {
      return deadlineWorkerRunning;
    }));
  }
  std::atomic<bool> expiredWorkRan{false};
  std::atomic<bool> expiredHandlerRan{false};
  assert(httpserver::DatabaseExecutor::Submit(
      [&] { expiredWorkRan.store(true); }, std::chrono::milliseconds(10),
      [&] { expiredHandlerRan.store(true); }));
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  releaseDeadlineWorker.store(true);
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  const httpserver::DatabaseExecutor::Stats expired =
      httpserver::DatabaseExecutor::GetStats();
  assert(!expiredWorkRan.load());
  assert(expiredHandlerRan.load());
  assert(expired.expired == 1);
  httpserver::DatabaseExecutor::Shutdown();
  return 0;
}
