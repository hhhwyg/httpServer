#pragma once
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <queue>
#include <utility>

class TimerNode {
 public:
  TimerNode(std::function<void()> onExpired, int timeout);
  ~TimerNode();
  void update(int timeout);
  bool isValid();
  void cancel();
  void setDeleted() { deleted_ = true; }
  bool isDeleted() const { return deleted_; }
  std::size_t getExpTime() const { return expiredTime_; }

 private:
  bool deleted_;
  std::size_t expiredTime_;
  std::function<void()> onExpired_;
};

struct TimerCmp {
  bool operator()(const std::shared_ptr<TimerNode>& a,
                  const std::shared_ptr<TimerNode>& b) const {
    return a->getExpTime() > b->getExpTime();
  }
};

class TimerManager {
 public:
  TimerManager();
  ~TimerManager();
  std::shared_ptr<TimerNode> addTimer(std::function<void()> onExpired,
                                      int timeout);
  void handleExpiredEvent();

 private:
  typedef std::shared_ptr<TimerNode> SPTimerNode;
  std::priority_queue<SPTimerNode, std::deque<SPTimerNode>, TimerCmp>
      timerNodeQueue;
  // MutexLock lock;
};
