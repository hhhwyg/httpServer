#include "httpserver/application/Metrics.h"

#include <atomic>
#include <array>
#include <sstream>

namespace httpserver {
namespace {

std::atomic<std::uint64_t> activeConnections{0};
std::atomic<std::uint64_t> totalConnections{0};
std::atomic<std::uint64_t> requests{0};
std::atomic<std::uint64_t> responses2xx{0};
std::atomic<std::uint64_t> responses4xx{0};
std::atomic<std::uint64_t> responses5xx{0};
std::atomic<std::uint64_t> ioErrors{0};
std::atomic<std::uint64_t> outputHighWater{0};
std::atomic<std::uint64_t> outputDropped{0};
std::atomic<std::uint64_t> slowConsumerClosed{0};

constexpr std::array<std::string_view, 5> kMethods = {
    "GET", "POST", "DELETE", "HEAD", "OTHER"};
constexpr std::array<std::string_view, 10> kRoutes = {
    "/ping", "/healthz", "/readyz", "/metrics", "/register", "/login",
    "/ws", "/api/v1/rooms", "/api/v1/rooms/:id", "other"};
constexpr std::array<std::string_view, 5> kStatusClasses = {
    "1xx", "2xx", "3xx", "4xx", "5xx"};
constexpr std::array<std::uint64_t, 8> kDurationBuckets = {
    1000, 5000, 10000, 25000, 50000, 100000, 500000, 1000000};
constexpr std::array<std::string_view, 3> kWebSocketTypes = {
    "subscribe", "message", "invalid"};

std::array<std::array<std::array<std::atomic<std::uint64_t>, 5>, 10>, 5>
    httpRequests{};
std::array<std::array<std::array<std::atomic<std::uint64_t>, 8>, 10>, 5>
    httpDurationBuckets{};
std::array<std::array<std::atomic<std::uint64_t>, 10>, 5> httpDurationCounts{};
std::array<std::array<std::atomic<std::uint64_t>, 10>, 5> httpDurationSums{};
std::array<std::array<std::atomic<std::uint64_t>, 2>, 3> webSocketMessages{};
std::array<std::atomic<std::uint64_t>, 8> webSocketBroadcastBuckets{};
std::atomic<std::uint64_t> webSocketBroadcastCount{0};
std::atomic<std::uint64_t> webSocketBroadcastSum{0};
std::atomic<std::uint64_t> webSocketConnections{0};

std::size_t MethodIndex(std::string_view method) {
  for (std::size_t index = 0; index + 1 < kMethods.size(); ++index) {
    if (method == kMethods[index]) return index;
  }
  return kMethods.size() - 1;
}

std::size_t RouteIndex(std::string_view path) {
  for (std::size_t index = 0; index < 7; ++index) {
    if (path == kRoutes[index]) return index;
  }
  if (path == "/api/v1/rooms") return 7;
  if (path.rfind("/api/v1/rooms/", 0) == 0) return 8;
  return 9;
}

std::size_t StatusClassIndex(int status) {
  if (status >= 100 && status < 600) return static_cast<std::size_t>(status / 100 - 1);
  return 4;
}

std::size_t WebSocketTypeIndex(std::string_view type) {
  if (type == "subscribe") return 0;
  if (type == "message") return 1;
  return 2;
}

}  // namespace

void Metrics::ConnectionOpened() {
  activeConnections.fetch_add(1, std::memory_order_relaxed);
  totalConnections.fetch_add(1, std::memory_order_relaxed);
}

void Metrics::ConnectionClosed() {
  std::uint64_t current = activeConnections.load(std::memory_order_relaxed);
  while (current > 0 && !activeConnections.compare_exchange_weak(
                            current, current - 1,
                            std::memory_order_relaxed)) {
  }
}

void Metrics::RequestStarted() { requests.fetch_add(1, std::memory_order_relaxed); }

void Metrics::ResponseSent(int status) {
  if (status >= 200 && status < 300) responses2xx.fetch_add(1, std::memory_order_relaxed);
  else if (status >= 400 && status < 500) responses4xx.fetch_add(1, std::memory_order_relaxed);
  else if (status >= 500) responses5xx.fetch_add(1, std::memory_order_relaxed);
}

void Metrics::HttpRequestCompleted(std::string_view method,
                                   std::string_view path, int status,
                                   std::uint64_t durationMicroseconds) {
  ResponseSent(status);
  const std::size_t methodIndex = MethodIndex(method);
  const std::size_t routeIndex = RouteIndex(path);
  const std::size_t statusIndex = StatusClassIndex(status);
  httpRequests[methodIndex][routeIndex][statusIndex].fetch_add(
      1, std::memory_order_relaxed);
  for (std::size_t bucket = 0; bucket < kDurationBuckets.size(); ++bucket) {
    if (durationMicroseconds <= kDurationBuckets[bucket]) {
      httpDurationBuckets[methodIndex][routeIndex][bucket].fetch_add(
          1, std::memory_order_relaxed);
    }
  }
  httpDurationCounts[methodIndex][routeIndex].fetch_add(1, std::memory_order_relaxed);
  httpDurationSums[methodIndex][routeIndex].fetch_add(durationMicroseconds,
                                                        std::memory_order_relaxed);
}

void Metrics::WebSocketConnectionOpened() {
  webSocketConnections.fetch_add(1, std::memory_order_relaxed);
}

void Metrics::WebSocketConnectionClosed() {
  std::uint64_t current = webSocketConnections.load(std::memory_order_relaxed);
  while (current > 0 && !webSocketConnections.compare_exchange_weak(
                            current, current - 1, std::memory_order_relaxed)) {
  }
}

void Metrics::WebSocketMessage(std::string_view type, bool accepted) {
  webSocketMessages[WebSocketTypeIndex(type)][accepted ? 0 : 1].fetch_add(
      1, std::memory_order_relaxed);
}

void Metrics::WebSocketBroadcast(std::uint64_t durationMicroseconds) {
  for (std::size_t bucket = 0; bucket < kDurationBuckets.size(); ++bucket) {
    if (durationMicroseconds <= kDurationBuckets[bucket]) {
      webSocketBroadcastBuckets[bucket].fetch_add(1, std::memory_order_relaxed);
    }
  }
  webSocketBroadcastCount.fetch_add(1, std::memory_order_relaxed);
  webSocketBroadcastSum.fetch_add(durationMicroseconds, std::memory_order_relaxed);
}

void Metrics::IoError() { ioErrors.fetch_add(1, std::memory_order_relaxed); }
void Metrics::OutputBufferHighWater() {
  outputHighWater.fetch_add(1, std::memory_order_relaxed);
}
void Metrics::OutputMessageDropped() {
  outputDropped.fetch_add(1, std::memory_order_relaxed);
}
void Metrics::SlowConsumerClosed() {
  slowConsumerClosed.fetch_add(1, std::memory_order_relaxed);
}

std::string Metrics::Render(int activeRooms, int databaseFree, int databaseInUse) {
  std::ostringstream output;
  output << "# TYPE httpserver_connections_active gauge\n"
         << "httpserver_connections_active " << activeConnections.load() << "\n"
         << "# TYPE httpserver_connections_total counter\n"
         << "httpserver_connections_total " << totalConnections.load() << "\n"
         << "# TYPE httpserver_requests_total counter\n"
         << "httpserver_requests_total " << requests.load() << "\n"
         << "httpserver_responses_total{class=\"2xx\"} " << responses2xx.load() << "\n"
         << "httpserver_responses_total{class=\"4xx\"} " << responses4xx.load() << "\n"
         << "httpserver_responses_total{class=\"5xx\"} " << responses5xx.load() << "\n"
         << "httpserver_io_errors_total " << ioErrors.load() << "\n"
         << "httpserver_output_buffer_high_water_total " << outputHighWater.load() << "\n"
         << "httpserver_output_messages_dropped_total " << outputDropped.load() << "\n"
         << "httpserver_slow_consumer_closed_total " << slowConsumerClosed.load() << "\n"
         << "httpserver_rooms_active " << activeRooms << "\n"
         << "httpserver_database_connections_free " << databaseFree << "\n"
         << "httpserver_database_connections_in_use " << databaseInUse << "\n";
  output << "# TYPE httpserver_http_requests_total counter\n"
         << "# TYPE httpserver_http_request_duration_seconds histogram\n";
  for (std::size_t method = 0; method < kMethods.size(); ++method) {
    for (std::size_t route = 0; route < kRoutes.size(); ++route) {
      for (std::size_t status = 0; status < kStatusClasses.size(); ++status) {
        const auto value = httpRequests[method][route][status].load();
        if (value != 0) output << "httpserver_http_requests_total{method=\""
            << kMethods[method] << "\",route=\"" << kRoutes[route]
            << "\",status_class=\"" << kStatusClasses[status] << "\"} "
            << value << "\n";
      }
      std::uint64_t cumulative = 0;
      for (std::size_t bucket = 0; bucket < kDurationBuckets.size(); ++bucket) {
        cumulative = httpDurationBuckets[method][route][bucket].load();
        if (cumulative != 0) output << "httpserver_http_request_duration_seconds_bucket{method=\""
            << kMethods[method] << "\",route=\"" << kRoutes[route]
            << "\",le=\"" << (kDurationBuckets[bucket] / 1000000.0)
            << "\"} " << cumulative << "\n";
      }
      const std::uint64_t count = httpDurationCounts[method][route].load();
      if (count != 0) {
        output << "httpserver_http_request_duration_seconds_bucket{method=\""
            << kMethods[method] << "\",route=\"" << kRoutes[route]
            << "\",le=\"+Inf\"} " << count << "\n"
            << "httpserver_http_request_duration_seconds_sum{method=\""
            << kMethods[method] << "\",route=\"" << kRoutes[route]
            << "\"} " << (httpDurationSums[method][route].load() / 1000000.0) << "\n"
            << "httpserver_http_request_duration_seconds_count{method=\""
            << kMethods[method] << "\",route=\"" << kRoutes[route]
            << "\"} " << count << "\n";
      }
    }
  }
  output << "# TYPE httpserver_websocket_connections gauge\n"
         << "httpserver_websocket_connections " << webSocketConnections.load() << "\n";
  for (std::size_t type = 0; type < kWebSocketTypes.size(); ++type) {
    output << "httpserver_websocket_messages_total{type=\"" << kWebSocketTypes[type]
           << "\",result=\"accepted\"} " << webSocketMessages[type][0].load() << "\n"
           << "httpserver_websocket_messages_total{type=\"" << kWebSocketTypes[type]
           << "\",result=\"rejected\"} " << webSocketMessages[type][1].load() << "\n";
  }
  output << "# TYPE httpserver_websocket_broadcast_duration_seconds histogram\n";
  for (std::size_t bucket = 0; bucket < kDurationBuckets.size(); ++bucket) {
    output << "httpserver_websocket_broadcast_duration_seconds_bucket{le=\""
           << (kDurationBuckets[bucket] / 1000000.0) << "\"} "
           << webSocketBroadcastBuckets[bucket].load() << "\n";
  }
  output << "httpserver_websocket_broadcast_duration_seconds_bucket{le=\"+Inf\"} "
         << webSocketBroadcastCount.load() << "\n"
         << "httpserver_websocket_broadcast_duration_seconds_sum "
         << (webSocketBroadcastSum.load() / 1000000.0) << "\n"
         << "httpserver_websocket_broadcast_duration_seconds_count "
         << webSocketBroadcastCount.load() << "\n";
  return output.str();
}

void Metrics::ResetForTesting() {
  activeConnections = 0;
  totalConnections = 0;
  requests = 0;
  responses2xx = 0;
  responses4xx = 0;
  responses5xx = 0;
  ioErrors = 0;
  outputHighWater = 0;
  outputDropped = 0;
  slowConsumerClosed = 0;
  webSocketConnections = 0;
  for (auto& method : httpRequests)
    for (auto& route : method)
      for (auto& status : route) status = 0;
  for (auto& method : httpDurationBuckets)
    for (auto& route : method)
      for (auto& bucket : route) bucket = 0;
  for (auto& method : httpDurationCounts)
    for (auto& route : method) route = 0;
  for (auto& method : httpDurationSums)
    for (auto& route : method) route = 0;
  for (auto& type : webSocketMessages)
    for (auto& result : type) result = 0;
  for (auto& bucket : webSocketBroadcastBuckets) bucket = 0;
  webSocketBroadcastCount = 0;
  webSocketBroadcastSum = 0;
}

}  // namespace httpserver
