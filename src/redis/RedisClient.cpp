#include "httpserver/application/RedisClient.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <queue>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>
#include <unistd.h>

#include "json.hpp"

#ifndef HTTPSERVER_HAS_HIREDIS
#define HTTPSERVER_HAS_HIREDIS 0
#endif

#if HTTPSERVER_HAS_HIREDIS
#include <hiredis/hiredis.h>

namespace httpserver {
namespace {

using Clock = std::chrono::steady_clock;
using json = nlohmann::json;

class RedisIoClient final : public RedisClient {
 public:
  explicit RedisIoClient(RedisConfig config)
      : config_(std::move(config)), instanceId_(ResolveInstanceId(config_)) {
    worker_ = std::thread(&RedisIoClient::Run, this);
    subscriber_ = std::thread(&RedisIoClient::RunSubscriber, this);
  }

  ~RedisIoClient() override {
    Shutdown();
  }

  RedisRateLimitResult AllowRateLimit(std::string_view scope, std::string_view identity,
                                      int maxAttempts, int windowSeconds) override {
    const std::string key =
        "chat:ratelimit:" + std::string(scope) + ':' + std::string(identity) + ':' +
        std::to_string(
            std::chrono::duration_cast<std::chrono::seconds>(Clock::now().time_since_epoch())
                .count() /
            windowSeconds);
    const std::string ttlMilliseconds = std::to_string(windowSeconds * 1000);
    const CommandResult result = Execute({"EVAL",
                                          "local c=redis.call('INCR',KEYS[1]);if c==1 then "
                                          "redis.call('PEXPIRE',KEYS[1],ARGV[1]) end;return c",
                                          "1", key, ttlMilliseconds});
    if (!result.ok) return RedisRateLimitResult::kUnavailable;
    return result.integer <= maxAttempts ? RedisRateLimitResult::kAllowed
                                         : RedisRateLimitResult::kLimited;
  }

  bool RevokeToken(std::string_view jti, int ttlSeconds) override {
    if (jti.empty() || ttlSeconds <= 0) return false;
    return Execute({"SET", "chat:jwt:denylist:" + std::string(jti), "1", "EX",
                    std::to_string(ttlSeconds)})
        .ok;
  }

  RedisTokenState TokenState(std::string_view jti) override {
    if (jti.empty()) return RedisTokenState::kUnavailable;
    const CommandResult result = Execute({"GET", "chat:jwt:denylist:" + std::string(jti)});
    if (!result.ok) return RedisTokenState::kUnavailable;
    return result.text.has_value() ? RedisTokenState::kRevoked : RedisTokenState::kNotRevoked;
  }

  bool InvalidateRoom(std::string_view roomId) override {
    return Submit({"DEL", "chat:room:" + std::string(roomId),
                   "chat:room:" + std::string(roomId) + ":recent"});
  }

  bool SetPresence(std::string_view userId, std::string_view instanceId, int ttlSeconds) override {
    return Submit({"SET", PresenceKey(userId, instanceId), "1", "EX", std::to_string(ttlSeconds)});
  }

  bool RemovePresence(std::string_view userId, std::string_view instanceId) override {
    return Submit({"DEL", PresenceKey(userId, instanceId)});
  }

  bool TrackPresence(std::string_view userId) override {
    if (userId.empty()) return false;
    {
      std::lock_guard<std::mutex> lock(presenceMutex_);
      ++presenceCounts_[std::string(userId)];
    }
    return SetPresence(userId, instanceId_, 30);
  }

  bool UntrackPresence(std::string_view userId) override {
    if (userId.empty()) return false;
    bool remove = false;
    {
      std::lock_guard<std::mutex> lock(presenceMutex_);
      const auto entry = presenceCounts_.find(std::string(userId));
      if (entry == presenceCounts_.end()) return true;
      if (--entry->second == 0) {
        presenceCounts_.erase(entry);
        remove = true;
      }
    }
    return !remove || RemovePresence(userId, instanceId_);
  }

  bool Publish(std::string_view channel, std::string_view payload) override {
    return Execute({"PUBLISH", std::string(channel), std::string(payload)}).ok;
  }

  std::optional<Room> GetRoom(std::string_view roomId) override {
    const CommandResult result = Execute({"GET", "chat:room:" + std::string(roomId)});
    if (!result.ok || !result.text.has_value()) return std::nullopt;
    try {
      const json value = json::parse(*result.text);
      return Room{value.at("id").get<std::string>(), value.at("name").get<std::string>(),
                  value.at("ownerId").get<std::string>()};
    } catch (const std::exception&) {
      return std::nullopt;
    }
  }

  bool PutRoom(const Room& room) override {
    return Submit({"SET", "chat:room:" + room.id,
                   json{{"id", room.id}, {"name", room.name}, {"ownerId", room.ownerId}}.dump(),
                   "EX", "300"});
  }

  std::optional<std::vector<Message>> GetRecentMessages(std::string_view roomId) override {
    const CommandResult result = Execute({"GET", "chat:room:" + std::string(roomId) + ":recent"});
    if (!result.ok || !result.text.has_value()) return std::nullopt;
    try {
      const json values = json::parse(*result.text);
      std::vector<Message> messages;
      for (const json& value : values) {
        messages.push_back(
            {value.at("id").get<std::string>(), value.at("roomId").get<std::string>(),
             value.at("senderId").get<std::string>(),
             value.at("clientMessageId").get<std::string>(), value.at("content").get<std::string>(),
             value.at("createdAt").get<std::string>()});
      }
      return messages;
    } catch (const std::exception&) {
      return std::nullopt;
    }
  }

  bool PutRecentMessages(std::string_view roomId, const std::vector<Message>& messages) override {
    json values = json::array();
    for (const Message& message : messages) {
      values.push_back({{"id", message.id},
                        {"roomId", message.roomId},
                        {"senderId", message.senderId},
                        {"clientMessageId", message.clientMessageId},
                        {"content", message.content},
                        {"createdAt", message.createdAt}});
    }
    return Submit(
        {"SET", "chat:room:" + std::string(roomId) + ":recent", values.dump(), "EX", "600"});
  }

  void RecordCacheHit() override {
    std::lock_guard<std::mutex> lock(mutex_);
    ++cacheHits_;
  }

  void RecordCacheMiss() override {
    std::lock_guard<std::mutex> lock(mutex_);
    ++cacheMisses_;
  }

  RedisStats Stats() const override {
    std::lock_guard<std::mutex> lock(mutex_);
    return {tasks_.size(), rejected_, errors_, cacheHits_, cacheMisses_, connected_};
  }

  void Shutdown() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stopping_) return;
      stopping_ = true;
    }
    available_.notify_all();
    if (worker_.joinable()) worker_.join();
    if (subscriber_.joinable()) subscriber_.join();
  }

  void SetEventHandler(RedisEventHandler handler) override {
    {
      std::lock_guard<std::mutex> lock(subscriberMutex_);
      eventHandler_ = std::move(handler);
    }
    available_.notify_all();
  }

 private:
  struct CommandResult {
    bool ok = false;
    long long integer = 0;
    std::optional<std::string> text;
  };

  struct Task {
    std::vector<std::string> arguments;
    std::mutex mutex;
    std::condition_variable done;
    bool completed = false;
    bool cancelled = false;
    CommandResult result;
  };

  static std::string PresenceKey(std::string_view userId, std::string_view instanceId) {
    return "chat:presence:" + std::string(userId) + ':' + std::string(instanceId);
  }

  static std::string ResolveInstanceId(const RedisConfig& config) {
    if (!config.instanceId.empty()) return config.instanceId;
    char hostname[256]{};
    if (gethostname(hostname, sizeof(hostname) - 1) != 0) {
      std::strcpy(hostname, "httpserver");
    }
    return std::string(hostname) + '-' + std::to_string(getpid());
  }

  void RefreshPresence(redisContext*& context) {
    std::vector<std::string> users;
    {
      std::lock_guard<std::mutex> lock(presenceMutex_);
      users.reserve(presenceCounts_.size());
      for (const auto& entry : presenceCounts_) users.push_back(entry.first);
    }
    ExecuteOnConnection(context, {"SET", "chat:instance:" + instanceId_, "1", "EX", "30"});
    for (const std::string& user : users) {
      ExecuteOnConnection(context, {"SET", PresenceKey(user, instanceId_), "1", "EX", "30"});
    }
  }

  CommandResult Execute(std::initializer_list<std::string> arguments) {
    auto task = std::make_shared<Task>();
    task->arguments.assign(arguments.begin(), arguments.end());
    if (!Enqueue(task)) return {};

    std::unique_lock<std::mutex> taskLock(task->mutex);
    if (!task->done.wait_for(taskLock,
                             std::chrono::milliseconds(config_.commandTimeoutMilliseconds),
                             [&] { return task->completed; })) {
      task->cancelled = true;
      return {};
    }
    return task->result;
  }

  bool Submit(std::initializer_list<std::string> arguments) {
    auto task = std::make_shared<Task>();
    task->arguments.assign(arguments.begin(), arguments.end());
    return Enqueue(task);
  }

  bool Enqueue(const std::shared_ptr<Task>& task) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stopping_ || tasks_.size() >= static_cast<std::size_t>(config_.queueCapacity)) {
        ++rejected_;
        return false;
      }
      tasks_.push(task);
    }
    available_.notify_one();
    return true;
  }

  redisContext* Connect() {
    const timeval timeout{config_.connectTimeoutMilliseconds / 1000,
                          (config_.connectTimeoutMilliseconds % 1000) * 1000};
    redisContext* context = redisConnectWithTimeout(config_.host.c_str(), config_.port, timeout);
    if (!context || context->err) {
      if (context) redisFree(context);
      return nullptr;
    }
    const timeval commandTimeout{config_.commandTimeoutMilliseconds / 1000,
                                 (config_.commandTimeoutMilliseconds % 1000) * 1000};
    redisSetTimeout(context, commandTimeout);
    if (!config_.password.empty()) {
      redisReply* reply = static_cast<redisReply*>(
          redisCommand(context, "AUTH %b", config_.password.data(), config_.password.size()));
      const bool authenticated = reply && reply->type != REDIS_REPLY_ERROR;
      if (reply) freeReplyObject(reply);
      if (!authenticated) {
        redisFree(context);
        return nullptr;
      }
    }
    return context;
  }

  bool WaitForStop(std::chrono::milliseconds delay) {
    std::unique_lock<std::mutex> lock(mutex_);
    return available_.wait_for(lock, delay, [&] { return stopping_; });
  }

  std::chrono::milliseconds ReconnectDelay(int failedAttempts) const {
    const int shift = std::min(failedAttempts - 1, 5);
    const int base = 50 * (1 << std::max(shift, 0));
    thread_local std::minstd_rand random(static_cast<unsigned>(getpid()));
    std::uniform_int_distribution<int> jitter(0, std::max(1, base / 4));
    return std::chrono::milliseconds(base + jitter(random));
  }

  redisContext* ConnectBefore(Clock::time_point deadline) {
    for (int attempt = 1; attempt <= config_.maxReconnectAttempts; ++attempt) {
      if (redisContext* context = Connect()) return context;
      if (attempt == config_.maxReconnectAttempts || Clock::now() >= deadline) {
        break;
      }
      const auto remaining =
          std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
      if (remaining.count() <= 0 || WaitForStop(std::min(ReconnectDelay(attempt), remaining))) {
        break;
      }
    }
    return nullptr;
  }

  CommandResult ExecuteOnConnection(redisContext*& context,
                                    const std::vector<std::string>& arguments) {
    // A command is never replayed after an I/O error: Redis may already have
    // applied it. Connection retries only happen before the command is sent.
    if (!context) {
      context = ConnectBefore(Clock::now() +
                              std::chrono::milliseconds(config_.commandTimeoutMilliseconds));
    }
    if (!context) return {};
    std::vector<const char*> values;
    std::vector<size_t> lengths;
    values.reserve(arguments.size());
    lengths.reserve(arguments.size());
    for (const std::string& argument : arguments) {
      values.push_back(argument.data());
      lengths.push_back(argument.size());
    }
    redisReply* reply = static_cast<redisReply*>(
        redisCommandArgv(context, static_cast<int>(values.size()), values.data(), lengths.data()));
    if (!reply || reply->type == REDIS_REPLY_ERROR) {
      if (reply) freeReplyObject(reply);
      redisFree(context);
      context = nullptr;
      std::lock_guard<std::mutex> lock(mutex_);
      ++errors_;
      connected_ = false;
      return {};
    }
    CommandResult result{true, reply->type == REDIS_REPLY_INTEGER ? reply->integer : 0,
                         reply->type == REDIS_REPLY_STRING && reply->str
                             ? std::optional<std::string>(std::string(reply->str, reply->len))
                             : std::nullopt};
    freeReplyObject(reply);
    return result;
  }

  void Run() {
    redisContext* context = nullptr;
    for (;;) {
      std::shared_ptr<Task> task;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!available_.wait_for(lock, std::chrono::seconds(10),
                                 [&] { return stopping_ || !tasks_.empty(); })) {
          lock.unlock();
          RefreshPresence(context);
          continue;
        }
        if (stopping_ && tasks_.empty()) break;
        task = tasks_.front();
        tasks_.pop();
      }
      {
        std::lock_guard<std::mutex> taskLock(task->mutex);
        if (task->cancelled) continue;
      }
      const CommandResult result = ExecuteOnConnection(context, task->arguments);
      {
        std::lock_guard<std::mutex> lock(mutex_);
        connected_ = context != nullptr;
      }
      {
        std::lock_guard<std::mutex> taskLock(task->mutex);
        if (!task->cancelled) {
          task->result = result;
          task->completed = true;
          task->done.notify_one();
        }
      }
    }
    if (context) redisFree(context);
  }

  void RunSubscriber() {
    redisContext* context = nullptr;
    int failedAttempts = 0;
    while (true) {
      RedisService::EventHandler handler;
      {
        std::lock_guard<std::mutex> lock(subscriberMutex_);
        handler = eventHandler_;
      }
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) break;
      }
      if (!handler) {
        WaitForStop(std::chrono::milliseconds(100));
        continue;
      }
      if (!context) {
        context = Connect();
        if (!context) {
          ++failedAttempts;
          if (WaitForStop(ReconnectDelay(std::min(failedAttempts, config_.maxReconnectAttempts)))) {
            break;
          }
          if (failedAttempts >= config_.maxReconnectAttempts) failedAttempts = 0;
          continue;
        }
        redisReply* subscribed =
            static_cast<redisReply*>(redisCommand(context, "SUBSCRIBE chat:events"));
        const bool subscribedOk = subscribed && subscribed->type != REDIS_REPLY_ERROR;
        if (subscribed) freeReplyObject(subscribed);
        if (!subscribedOk) {
          redisFree(context);
          context = nullptr;
          ++failedAttempts;
          continue;
        }
        failedAttempts = 0;
      }
      void* rawReply = nullptr;
      if (redisGetReply(context, &rawReply) != REDIS_OK) {
        if (rawReply) freeReplyObject(rawReply);
        redisFree(context);
        context = nullptr;
        std::lock_guard<std::mutex> lock(mutex_);
        ++errors_;
        ++failedAttempts;
        continue;
      }
      redisReply* reply = static_cast<redisReply*>(rawReply);
      if (reply && reply->type == REDIS_REPLY_ARRAY && reply->elements == 3 &&
          reply->element[0]->str && std::strcmp(reply->element[0]->str, "message") == 0 &&
          reply->element[2]->str) {
        handler(std::string_view(reply->element[2]->str, reply->element[2]->len));
      }
      if (reply) freeReplyObject(reply);
    }
    if (context) redisFree(context);
  }

  const RedisConfig config_;
  const std::string instanceId_;
  mutable std::mutex mutex_;
  std::condition_variable available_;
  std::queue<std::shared_ptr<Task>> tasks_;
  std::thread worker_;
  std::thread subscriber_;
  std::mutex subscriberMutex_;
  RedisService::EventHandler eventHandler_;
  std::mutex presenceMutex_;
  std::unordered_map<std::string, std::size_t> presenceCounts_;
  std::size_t rejected_ = 0;
  std::size_t errors_ = 0;
  std::size_t cacheHits_ = 0;
  std::size_t cacheMisses_ = 0;
  bool connected_ = false;
  bool stopping_ = false;
};

std::mutex& serviceMutex() {
  static std::mutex mutex;
  return mutex;
}

std::shared_ptr<RedisClient>& service() {
  static std::shared_ptr<RedisClient> client;
  return client;
}

}  // namespace

void RedisService::Configure(const RedisConfig& config) {
  std::shared_ptr<RedisClient> previous;
  {
    std::lock_guard<std::mutex> lock(serviceMutex());
    previous = std::move(service());
    if (config.enabled()) service() = std::make_shared<RedisIoClient>(config);
  }
}

void RedisService::Shutdown() {
  std::shared_ptr<RedisClient> previous;
  {
    std::lock_guard<std::mutex> lock(serviceMutex());
    previous = std::move(service());
  }
}

void RedisService::SetClientForTesting(std::shared_ptr<RedisClient> client) {
  std::shared_ptr<RedisClient> previous;
  {
    std::lock_guard<std::mutex> lock(serviceMutex());
    previous = std::move(service());
    service() = std::move(client);
  }
}

bool RedisService::enabled() {
  std::lock_guard<std::mutex> lock(serviceMutex());
  return service() != nullptr;
}

RedisRateLimitResult RedisService::AllowRateLimit(std::string_view scope, std::string_view identity,
                                                  int maxAttempts, int windowSeconds) {
  std::shared_ptr<RedisClient> client;
  {
    std::lock_guard<std::mutex> lock(serviceMutex());
    client = service();
  }
  return client ? client->AllowRateLimit(scope, identity, maxAttempts, windowSeconds)
                : RedisRateLimitResult::kUnavailable;
}

bool RedisService::RevokeToken(std::string_view jti, int ttlSeconds) {
  std::shared_ptr<RedisClient> client;
  {
    std::lock_guard<std::mutex> lock(serviceMutex());
    client = service();
  }
  return client && client->RevokeToken(jti, ttlSeconds);
}

RedisTokenState RedisService::TokenState(std::string_view jti) {
  std::shared_ptr<RedisClient> client;
  {
    std::lock_guard<std::mutex> lock(serviceMutex());
    client = service();
  }
  return client ? client->TokenState(jti) : RedisTokenState::kUnavailable;
}

bool RedisService::InvalidateRoom(std::string_view roomId) {
  std::shared_ptr<RedisClient> client;
  {
    std::lock_guard<std::mutex> lock(serviceMutex());
    client = service();
  }
  return client && client->InvalidateRoom(roomId);
}

bool RedisService::SetPresence(std::string_view userId, std::string_view instanceId,
                               int ttlSeconds) {
  std::shared_ptr<RedisClient> client;
  {
    std::lock_guard<std::mutex> lock(serviceMutex());
    client = service();
  }
  return client && client->SetPresence(userId, instanceId, ttlSeconds);
}

bool RedisService::RemovePresence(std::string_view userId, std::string_view instanceId) {
  std::shared_ptr<RedisClient> client;
  {
    std::lock_guard<std::mutex> lock(serviceMutex());
    client = service();
  }
  return client && client->RemovePresence(userId, instanceId);
}

bool RedisService::TrackPresence(std::string_view userId) {
  std::shared_ptr<RedisClient> client;
  {
    std::lock_guard<std::mutex> lock(serviceMutex());
    client = service();
  }
  return client && client->TrackPresence(userId);
}

bool RedisService::UntrackPresence(std::string_view userId) {
  std::shared_ptr<RedisClient> client;
  {
    std::lock_guard<std::mutex> lock(serviceMutex());
    client = service();
  }
  return client && client->UntrackPresence(userId);
}

bool RedisService::Publish(std::string_view channel, std::string_view payload) {
  std::shared_ptr<RedisClient> client;
  {
    std::lock_guard<std::mutex> lock(serviceMutex());
    client = service();
  }
  return client && client->Publish(channel, payload);
}

std::optional<Room> RedisService::GetRoom(std::string_view roomId) {
  std::shared_ptr<RedisClient> client;
  {
    std::lock_guard<std::mutex> lock(serviceMutex());
    client = service();
  }
  return client ? client->GetRoom(roomId) : std::nullopt;
}

bool RedisService::PutRoom(const Room& room) {
  std::shared_ptr<RedisClient> client;
  {
    std::lock_guard<std::mutex> lock(serviceMutex());
    client = service();
  }
  return client && client->PutRoom(room);
}

std::optional<std::vector<Message>> RedisService::GetRecentMessages(std::string_view roomId) {
  std::shared_ptr<RedisClient> client;
  {
    std::lock_guard<std::mutex> lock(serviceMutex());
    client = service();
  }
  return client ? client->GetRecentMessages(roomId) : std::nullopt;
}

bool RedisService::PutRecentMessages(std::string_view roomId,
                                     const std::vector<Message>& messages) {
  std::shared_ptr<RedisClient> client;
  {
    std::lock_guard<std::mutex> lock(serviceMutex());
    client = service();
  }
  return client && client->PutRecentMessages(roomId, messages);
}

void RedisService::RecordCacheHit() {
  std::shared_ptr<RedisClient> client;
  {
    std::lock_guard<std::mutex> lock(serviceMutex());
    client = service();
  }
  if (client) client->RecordCacheHit();
}

void RedisService::RecordCacheMiss() {
  std::shared_ptr<RedisClient> client;
  {
    std::lock_guard<std::mutex> lock(serviceMutex());
    client = service();
  }
  if (client) client->RecordCacheMiss();
}

void RedisService::SetEventHandler(EventHandler handler) {
  std::shared_ptr<RedisClient> client;
  {
    std::lock_guard<std::mutex> lock(serviceMutex());
    client = service();
  }
  if (client) client->SetEventHandler(std::move(handler));
}

RedisStats RedisService::Stats() {
  std::shared_ptr<RedisClient> client;
  {
    std::lock_guard<std::mutex> lock(serviceMutex());
    client = service();
  }
  return client ? client->Stats() : RedisStats{};
}

}  // namespace httpserver

#else

namespace httpserver {

void RedisService::Configure(const RedisConfig&) {}
void RedisService::Shutdown() {}
void RedisService::SetClientForTesting(std::shared_ptr<RedisClient>) {}
bool RedisService::enabled() {
  return false;
}
RedisRateLimitResult RedisService::AllowRateLimit(std::string_view, std::string_view, int, int) {
  return RedisRateLimitResult::kUnavailable;
}
bool RedisService::RevokeToken(std::string_view, int) {
  return false;
}
RedisTokenState RedisService::TokenState(std::string_view) {
  return RedisTokenState::kUnavailable;
}
bool RedisService::InvalidateRoom(std::string_view) {
  return false;
}
bool RedisService::SetPresence(std::string_view, std::string_view, int) {
  return false;
}
bool RedisService::RemovePresence(std::string_view, std::string_view) {
  return false;
}
bool RedisService::TrackPresence(std::string_view) {
  return false;
}
bool RedisService::UntrackPresence(std::string_view) {
  return false;
}
bool RedisService::Publish(std::string_view, std::string_view) {
  return false;
}
std::optional<Room> RedisService::GetRoom(std::string_view) {
  return std::nullopt;
}
bool RedisService::PutRoom(const Room&) {
  return false;
}
std::optional<std::vector<Message>> RedisService::GetRecentMessages(std::string_view) {
  return std::nullopt;
}
bool RedisService::PutRecentMessages(std::string_view, const std::vector<Message>&) {
  return false;
}
void RedisService::RecordCacheHit() {}
void RedisService::RecordCacheMiss() {}
void RedisService::SetEventHandler(EventHandler) {}
RedisStats RedisService::Stats() {
  return {};
}

}  // namespace httpserver

#endif
