#include <cassert>
#include <atomic>
#include <stdexcept>
#include <thread>
#include <vector>

#include "base/ObjectPool.h"

namespace {

struct TrackedObject {
  static int constructions;
  static int destructions;

  explicit TrackedObject(int value) : value(value) { ++constructions; }
  ~TrackedObject() { ++destructions; }

  int value;
};

int TrackedObject::constructions = 0;
int TrackedObject::destructions = 0;

struct ThrowingObject {
  explicit ThrowingObject(bool shouldThrow) {
    if (shouldThrow) throw std::runtime_error("construction failed");
  }
};

struct ConcurrentObject {
  explicit ConcurrentObject(int value) : value(value) {}
  int value;
};

void testReuseDestroysOldObject() {
  auto& pool = ObjectPool<TrackedObject>::Instance();
  pool.setMaxCached(1);

  auto first = pool.acquire(41);
  TrackedObject* firstAddress = first.get();
  assert(first->value == 41);

  first.reset();
  assert(TrackedObject::destructions == 1);

  auto second = pool.acquire(99);
  assert(second.get() == firstAddress);
  assert(second->value == 99);
  assert(TrackedObject::constructions == 2);

  second.reset();
  assert(TrackedObject::destructions == 2);
  assert(pool.cachedCount() == 1);

  pool.setMaxCached(0);
  assert(pool.maxCached() == 0);
  assert(pool.cachedCount() == 0);
}

void testConstructionFailureReturnsCachedStorage() {
  auto& pool = ObjectPool<ThrowingObject>::Instance();
  pool.setMaxCached(1);

  auto object = pool.acquire(false);
  object.reset();
  assert(pool.cachedCount() == 1);

  bool threw = false;
  try {
    static_cast<void>(pool.acquire(true));
  } catch (const std::runtime_error&) {
    threw = true;
  }
  assert(threw);
  assert(pool.cachedCount() == 1);

  auto replacement = pool.acquire(false);
  replacement.reset();
  pool.setMaxCached(0);
}

void testConcurrentAcquireAndReleaseRespectsCacheLimit() {
  auto& pool = ObjectPool<ConcurrentObject>::Instance();
  constexpr int kThreadCount = 8;
  constexpr int kIterationsPerThread = 500;
  constexpr std::size_t kMaxCached = 16;
  pool.setMaxCached(kMaxCached);

  std::atomic<bool> failed{false};
  std::vector<std::thread> threads;
  threads.reserve(kThreadCount);
  for (int threadIndex = 0; threadIndex < kThreadCount; ++threadIndex) {
    threads.emplace_back([threadIndex, &pool, &failed] {
      for (int iteration = 0; iteration < kIterationsPerThread; ++iteration) {
        auto object = pool.acquire(threadIndex + iteration);
        if (object->value != threadIndex + iteration) {
          failed.store(true, std::memory_order_relaxed);
        }
      }
    });
  }
  for (std::thread& thread : threads) {
    thread.join();
  }

  assert(!failed.load(std::memory_order_relaxed));
  assert(pool.cachedCount() <= kMaxCached);
  pool.setMaxCached(0);
}

}  // namespace

int main() {
  testReuseDestroysOldObject();
  testConstructionFailureReturnsCachedStorage();
  testConcurrentAcquireAndReleaseRespectsCacheLimit();
  return 0;
}
