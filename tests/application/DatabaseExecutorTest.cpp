#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <mutex>

#include "httpserver/application/DatabaseExecutor.h"

int main() {
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
  return 0;
}
