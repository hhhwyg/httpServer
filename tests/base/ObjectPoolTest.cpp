#include <cassert>

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

}  // namespace

int main() {
  auto& pool = ObjectPool<TrackedObject>::Instance();
  pool.setMaxCached(1);

  auto first = pool.acquire(41);
  TrackedObject* first_address = first.get();
  assert(first->value == 41);

  first.reset();
  assert(TrackedObject::destructions == 1);

  auto second = pool.acquire(99);
  assert(second.get() == first_address);
  assert(second->value == 99);
  assert(TrackedObject::constructions == 2);

  second.reset();
  assert(TrackedObject::destructions == 2);
  assert(pool.cachedCount() == 1);

  pool.setMaxCached(0);
  assert(pool.maxCached() == 0);
  assert(pool.cachedCount() == 0);
  return 0;
}
