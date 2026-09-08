#include "httpserver/application/OutboxPublisher.h"

#include <atomic>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

#include "httpserver/application/RedisClient.h"
#include "httpserver/application/StructuredLog.h"
#include "SqlConnPool.h"
#include "json.hpp"

namespace httpserver {
namespace {

using json = nlohmann::json;

struct Event {
  std::string id;
  std::string payload;
};

class Publisher {
 public:
  explicit Publisher(OutboxConfig config) : config_(std::move(config)) {
    if (config_.publisherId.empty()) {
      config_.publisherId = "outbox-" + std::to_string(::getpid());
    }
  }
  ~Publisher() {
    Stop();
  }

  void Start() {
    thread_ = std::thread(&Publisher::Run, this);
  }
  void Stop() {
    stopping_.store(true, std::memory_order_release);
    wake_.notify_all();
    if (thread_.joinable()) thread_.join();
  }
  OutboxPublisher::Stats Stats() const {
    return {claimed_.load(), published_.load(), failed_.load(), pending_.load(),
            oldestAgeSeconds_.load()};
  }

 private:
  std::vector<Event> Claim() {
    MYSQL* db = nullptr;
    SqlConnRAII lease(&db, SqlConnPool::Instance());
    if (!db || mysql_autocommit(db, 0) != 0) return {};
    ObserveBacklog(db);
    const std::string select =
        "SELECT event_id, CAST(payload AS CHAR) FROM message_outbox "
        "WHERE published_at IS NULL AND next_attempt_at <= NOW(3) "
        "AND (lease_until IS NULL OR lease_until < NOW(3)) "
        "ORDER BY created_at LIMIT " +
        std::to_string(config_.batchSize) + " FOR UPDATE SKIP LOCKED";
    std::vector<Event> events;
    if (mysql_query(db, select.c_str()) == 0) {
      MYSQL_RES* result = mysql_store_result(db);
      if (result) {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(result)) != nullptr && row[0] && row[1]) {
          events.push_back({row[0], row[1]});
        }
        mysql_free_result(result);
      }
    }
    // Prepared statements are unnecessary for generated UUIDs but lease owner
    // is configurable, so quote it through the connection before composing SQL.
    std::string owner(config_.publisherId.size() * 2 + 1, '\0');
    const unsigned long ownerLength = mysql_real_escape_string(
        db, owner.data(), config_.publisherId.data(), config_.publisherId.size());
    owner.resize(ownerLength);
    for (const Event& event : events) {
      std::string id(event.id.size() * 2 + 1, '\0');
      const unsigned long idLength =
          mysql_real_escape_string(db, id.data(), event.id.data(), event.id.size());
      id.resize(idLength);
      const std::string update = "UPDATE message_outbox SET lease_owner='" + owner +
                                 "', lease_until=DATE_ADD(NOW(3), INTERVAL " +
                                 std::to_string(config_.leaseSeconds) +
                                 " SECOND) WHERE event_id='" + id + "'";
      if (mysql_query(db, update.c_str()) != 0) events.clear();
    }
    if (events.empty() || mysql_commit(db) != 0) {
      mysql_rollback(db);
      mysql_autocommit(db, 1);
      return {};
    }
    mysql_autocommit(db, 1);
    claimed_.fetch_add(events.size(), std::memory_order_relaxed);
    return events;
  }

  void ObserveBacklog(MYSQL* db) {
    constexpr char kQuery[] =
        "SELECT COUNT(*), COALESCE(TIMESTAMPDIFF(SECOND, MIN(created_at), NOW()), 0) "
        "FROM message_outbox WHERE published_at IS NULL";
    if (mysql_query(db, kQuery) != 0) return;
    MYSQL_RES* result = mysql_store_result(db);
    if (!result) return;
    MYSQL_ROW row = mysql_fetch_row(result);
    if (row && row[0] && row[1]) {
      pending_.store(static_cast<std::size_t>(std::strtoull(row[0], nullptr, 10)),
                     std::memory_order_relaxed);
      oldestAgeSeconds_.store(static_cast<std::size_t>(std::strtoull(row[1], nullptr, 10)),
                              std::memory_order_relaxed);
    }
    mysql_free_result(result);
  }

  void Finish(const Event& event, bool published, std::string_view messageId = {}) {
    MYSQL* db = nullptr;
    SqlConnRAII lease(&db, SqlConnPool::Instance());
    if (!db) return;
    std::string id(event.id.size() * 2 + 1, '\0');
    const unsigned long idLength =
        mysql_real_escape_string(db, id.data(), event.id.data(), event.id.size());
    id.resize(idLength);
    const int maxBackoff = std::min(config_.maxBackoffSeconds, 3600);
    const std::string query =
        published
            ? "UPDATE message_outbox SET published_at=NOW(3), lease_owner=NULL, lease_until=NULL "
              "WHERE event_id='" +
                  id + "'"
            : "UPDATE message_outbox SET attempts=attempts+1, lease_owner=NULL, lease_until=NULL, "
              "next_attempt_at=DATE_ADD(NOW(3), INTERVAL LEAST(" +
                  std::to_string(maxBackoff) +
                  ", POW(2, LEAST(attempts, 16)) + FLOOR(RAND() * 2)) SECOND) WHERE event_id='" +
                  id + "'";
    mysql_query(db, query.c_str());
    (published ? published_ : failed_).fetch_add(1, std::memory_order_relaxed);
    LogStructuredEvent(
        {{}, {}, event.id, messageId, "outbox.publish", published ? "" : "redis_unavailable", 0});
  }

  void Run() {
    while (!stopping_.load(std::memory_order_acquire)) {
      for (const Event& event : Claim()) {
        try {
          json envelope = json::parse(event.payload);
          // MySQL JSON preserves numeric message/room IDs. The WebSocket
          // protocol deliberately uses strings for all identifiers.
          for (const char* field : {"messageId", "roomId"}) {
            if (envelope.contains(field) && envelope[field].is_number_unsigned()) {
              envelope[field] = std::to_string(envelope[field].get<std::uint64_t>());
            }
          }
          envelope["eventId"] = event.id;
          envelope["eventType"] = "message.created";
          envelope["originInstanceId"] = config_.publisherId;
          const std::string messageId = envelope.value("messageId", "");
          Finish(event, RedisService::Publish("chat:events", envelope.dump()), messageId);
        } catch (const std::exception&) {
          Finish(event, false);
        }
      }
      std::unique_lock<std::mutex> lock(mutex_);
      wake_.wait_for(lock, std::chrono::milliseconds(config_.pollIntervalMilliseconds),
                     [&] { return stopping_.load(std::memory_order_acquire); });
    }
  }

  OutboxConfig config_;
  std::atomic<bool> stopping_{false};
  std::mutex mutex_;
  std::condition_variable wake_;
  std::atomic<std::size_t> claimed_{0};
  std::atomic<std::size_t> published_{0};
  std::atomic<std::size_t> failed_{0};
  std::atomic<std::size_t> pending_{0};
  std::atomic<std::size_t> oldestAgeSeconds_{0};
  std::thread thread_;
};

std::mutex& stateMutex() {
  static std::mutex value;
  return value;
}
std::unique_ptr<Publisher>& state() {
  static std::unique_ptr<Publisher> value;
  return value;
}
OutboxConfig& pendingConfig() {
  static OutboxConfig value;
  return value;
}

}  // namespace

void OutboxPublisher::Configure(const OutboxConfig& config) {
  std::lock_guard<std::mutex> lock(stateMutex());
  pendingConfig() = config;
}
void OutboxPublisher::Start() {
  std::lock_guard<std::mutex> lock(stateMutex());
  if (!state() && SqlConnPool::Instance()->IsInitialized()) {
    state() = std::make_unique<Publisher>(pendingConfig());
    state()->Start();
  }
}
void OutboxPublisher::Shutdown() {
  std::unique_ptr<Publisher> publisher;
  {
    std::lock_guard<std::mutex> lock(stateMutex());
    publisher = std::move(state());
  }
}
OutboxPublisher::Stats OutboxPublisher::GetStats() {
  std::lock_guard<std::mutex> lock(stateMutex());
  return state() ? state()->Stats() : Stats{};
}

}  // namespace httpserver
