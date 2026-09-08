#include "httpserver/application/OperationalStatus.h"

#include <atomic>

#include "httpserver/application/ChatService.h"
#include "httpserver/application/DatabaseExecutor.h"
#include "CryptoUtil.h"
#include "MySqlChatRepository.h"
#include "SqlConnPool.h"
#include "httpserver/application/Metrics.h"
#include "httpserver/application/RedisClient.h"
#include "httpserver/application/OutboxPublisher.h"
#include "httpserver/application/UserRepository.h"

namespace httpserver {
namespace {

std::atomic<bool> databaseEnabled{false};
std::atomic<bool> databaseHealthy{true};
std::atomic<bool> probeInFlight{false};

void RefreshDatabaseHealth() {
  if (!databaseEnabled.load(std::memory_order_acquire)) return;
  bool expected = false;
  if (!probeInFlight.compare_exchange_strong(expected, true,
                                              std::memory_order_acq_rel)) {
    return;
  }
  if (!DatabaseExecutor::Submit([] {
        databaseHealthy.store(MySqlChatSchemaCurrent(),
                              std::memory_order_release);
        probeInFlight.store(false, std::memory_order_release);
      })) {
    databaseHealthy.store(false, std::memory_order_release);
    probeInFlight.store(false, std::memory_order_release);
  }
}

}  // namespace

void OperationalStatus::ConfigureDatabaseReadiness(bool enabled) {
  databaseEnabled.store(enabled, std::memory_order_release);
  databaseHealthy.store(!enabled || MySqlChatSchemaCurrent(),
                         std::memory_order_release);
  probeInFlight.store(false, std::memory_order_release);
}

bool OperationalStatus::ready() {
  RefreshDatabaseHealth();
  return CryptoUtil::jwtEnabled() && GetUserRepository().available() &&
         ChatService::Instance().persistenceAvailable() &&
         databaseHealthy.load(std::memory_order_acquire);
}

std::string OperationalStatus::prometheusMetrics() {
  SqlConnPool* pool = SqlConnPool::Instance();
  std::string output = Metrics::Render(
      static_cast<int>(ChatService::Instance().CachedRoomCount()),
      pool->GetFreeConnCount(), pool->GetUseConnCount());
  const RedisStats redis = RedisService::Stats();
  const DatabaseExecutor::Stats databaseExecutor = DatabaseExecutor::GetStats();
  output += "httpserver_database_executor_queue_depth " +
            std::to_string(databaseExecutor.queued) + "\n";
  output += "httpserver_database_executor_running " +
            std::to_string(databaseExecutor.running) + "\n";
  output += "httpserver_database_executor_rejected_total " +
            std::to_string(databaseExecutor.rejected) + "\n";
  output += "httpserver_database_executor_expired_total " +
            std::to_string(databaseExecutor.expired) + "\n";
  output += "httpserver_redis_queue_depth " + std::to_string(redis.queued) + "\n";
  output += "httpserver_redis_rejected_total " + std::to_string(redis.rejected) + "\n";
  output += "httpserver_redis_errors_total " + std::to_string(redis.errors) + "\n";
  output += "httpserver_redis_cache_hits_total " + std::to_string(redis.cacheHits) + "\n";
  output += "httpserver_redis_cache_misses_total " + std::to_string(redis.cacheMisses) + "\n";
  const OutboxPublisher::Stats outbox = OutboxPublisher::GetStats();
  output += "httpserver_outbox_claimed_total " + std::to_string(outbox.claimed) + "\n";
  output += "httpserver_outbox_published_total " + std::to_string(outbox.published) + "\n";
  output += "httpserver_outbox_publish_failures_total " + std::to_string(outbox.failed) + "\n";
  output += "httpserver_outbox_pending " + std::to_string(outbox.pending) + "\n";
  output += "httpserver_outbox_oldest_age_seconds " +
            std::to_string(outbox.oldestAgeSeconds) + "\n";
  return output;
}

}  // namespace httpserver
