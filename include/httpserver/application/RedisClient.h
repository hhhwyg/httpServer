#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "httpserver/config/ServerConfig.h"
#include "httpserver/domain/ChatDomain.h"

namespace httpserver {

enum class RedisRateLimitResult { kAllowed, kLimited, kUnavailable };
enum class RedisTokenState { kNotRevoked, kRevoked, kUnavailable };
using RedisEventHandler = std::function<void(std::string_view)>;

struct RedisStats {
  std::size_t queued = 0;
  std::size_t rejected = 0;
  std::size_t errors = 0;
  std::size_t cacheHits = 0;
  std::size_t cacheMisses = 0;
  bool connected = false;
};

// Semantic boundary for Redis. Business code never sees hiredis replies or
// Redis command strings.
class RedisClient {
 public:
  virtual ~RedisClient() = default;
  virtual RedisRateLimitResult AllowRateLimit(std::string_view scope,
                                               std::string_view identity,
                                               int maxAttempts,
                                               int windowSeconds) = 0;
  virtual bool RevokeToken(std::string_view jti, int ttlSeconds) = 0;
  virtual RedisTokenState TokenState(std::string_view jti) = 0;
  virtual bool InvalidateRoom(std::string_view roomId) = 0;
  virtual bool SetPresence(std::string_view userId, std::string_view instanceId,
                           int ttlSeconds) = 0;
  virtual bool RemovePresence(std::string_view userId,
                              std::string_view instanceId) = 0;
  virtual bool TrackPresence(std::string_view userId) = 0;
  virtual bool UntrackPresence(std::string_view userId) = 0;
  virtual bool Publish(std::string_view channel, std::string_view payload) = 0;
  virtual std::optional<Room> GetRoom(std::string_view roomId) = 0;
  virtual bool PutRoom(const Room& room) = 0;
  virtual std::optional<std::vector<Message>> GetRecentMessages(
      std::string_view roomId) = 0;
  virtual bool PutRecentMessages(std::string_view roomId,
                                 const std::vector<Message>& messages) = 0;
  virtual void RecordCacheHit() = 0;
  virtual void RecordCacheMiss() = 0;
  virtual void SetEventHandler(RedisEventHandler handler) = 0;
  virtual RedisStats Stats() const = 0;
};

class RedisService {
 public:
  using EventHandler = RedisEventHandler;
  static void Configure(const RedisConfig& config);
  static void Shutdown();
  // Test-only dependency injection. Production config always installs the
  // hiredis-backed implementation.
  static void SetClientForTesting(std::shared_ptr<RedisClient> client);
  static bool enabled();
  static RedisRateLimitResult AllowRateLimit(std::string_view scope,
                                             std::string_view identity,
                                             int maxAttempts,
                                             int windowSeconds);
  static bool RevokeToken(std::string_view jti, int ttlSeconds);
  static RedisTokenState TokenState(std::string_view jti);
  static bool InvalidateRoom(std::string_view roomId);
  static bool SetPresence(std::string_view userId, std::string_view instanceId,
                          int ttlSeconds = 30);
  static bool RemovePresence(std::string_view userId,
                             std::string_view instanceId);
  static bool TrackPresence(std::string_view userId);
  static bool UntrackPresence(std::string_view userId);
  static bool Publish(std::string_view channel, std::string_view payload);
  static std::optional<Room> GetRoom(std::string_view roomId);
  static bool PutRoom(const Room& room);
  static std::optional<std::vector<Message>> GetRecentMessages(
      std::string_view roomId);
  static bool PutRecentMessages(std::string_view roomId,
                                const std::vector<Message>& messages);
  static void RecordCacheHit();
  static void RecordCacheMiss();
  // The callback runs on the dedicated Redis subscription thread. Callers
  // that touch transport state must queue work to their owning EventLoop.
  static void SetEventHandler(EventHandler handler);
  static RedisStats Stats();
};

}  // namespace httpserver
