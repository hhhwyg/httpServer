#include <cassert>
#include <memory>
#include <string>

#include "httpserver/application/RedisClient.h"

namespace {

class FakeRedisClient final : public httpserver::RedisClient {
 public:
  httpserver::RedisRateLimitResult AllowRateLimit(
      std::string_view, std::string_view, int, int) override {
    return httpserver::RedisRateLimitResult::kAllowed;
  }
  bool RevokeToken(std::string_view, int) override { return true; }
  httpserver::RedisTokenState TokenState(std::string_view) override {
    return httpserver::RedisTokenState::kNotRevoked;
  }
  bool InvalidateRoom(std::string_view) override { return true; }
  bool SetPresence(std::string_view, std::string_view, int) override { return true; }
  bool RemovePresence(std::string_view, std::string_view) override { return true; }
  bool TrackPresence(std::string_view) override { return true; }
  bool UntrackPresence(std::string_view) override { return true; }
  bool Publish(std::string_view, std::string_view payload) override {
    if (handler) handler(payload);
    return true;
  }
  std::optional<httpserver::Room> GetRoom(std::string_view) override {
    return room;
  }
  bool PutRoom(const httpserver::Room& value) override {
    room = value;
    return true;
  }
  std::optional<std::vector<httpserver::Message>> GetRecentMessages(
      std::string_view) override {
    return messages;
  }
  bool PutRecentMessages(std::string_view,
                         const std::vector<httpserver::Message>& value) override {
    messages = value;
    return true;
  }
  void RecordCacheHit() override { ++stats.cacheHits; }
  void RecordCacheMiss() override { ++stats.cacheMisses; }
  void SetEventHandler(httpserver::RedisEventHandler value) override {
    handler = std::move(value);
  }
  httpserver::RedisStats Stats() const override { return stats; }

 private:
  std::optional<httpserver::Room> room;
  std::optional<std::vector<httpserver::Message>> messages;
  httpserver::RedisEventHandler handler;
  httpserver::RedisStats stats;
};

}  // namespace

int main() {
  auto fake = std::make_shared<FakeRedisClient>();
  httpserver::RedisService::SetClientForTesting(fake);
  assert(httpserver::RedisService::enabled());
  assert(httpserver::RedisService::AllowRateLimit("test", "user", 1, 60) ==
         httpserver::RedisRateLimitResult::kAllowed);
  assert(httpserver::RedisService::PutRoom({"room-1", "Room", "owner"}));
  assert(httpserver::RedisService::GetRoom("room-1")->name == "Room");
  httpserver::RedisService::RecordCacheHit();
  httpserver::RedisService::RecordCacheMiss();
  assert(httpserver::RedisService::Stats().cacheHits == 1);
  assert(httpserver::RedisService::Stats().cacheMisses == 1);
  std::string event;
  httpserver::RedisService::SetEventHandler(
      [&event](std::string_view payload) { event = payload; });
  assert(httpserver::RedisService::Publish("chat:events", "event"));
  assert(event == "event");
  httpserver::RedisService::SetClientForTesting(nullptr);
  return 0;
}
