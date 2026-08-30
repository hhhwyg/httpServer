#include "httpserver/application/Metrics.h"

#include <atomic>
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

void Metrics::IoError() { ioErrors.fetch_add(1, std::memory_order_relaxed); }

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
         << "httpserver_rooms_active " << activeRooms << "\n"
         << "httpserver_database_connections_free " << databaseFree << "\n"
         << "httpserver_database_connections_in_use " << databaseInUse << "\n";
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
}

}  // namespace httpserver
