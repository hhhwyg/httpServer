#include <cassert>
#include <string>

#include "httpserver/application/Metrics.h"

int main() {
  httpserver::Metrics::ResetForTesting();
  httpserver::Metrics::ConnectionOpened();
  httpserver::Metrics::ConnectionOpened();
  httpserver::Metrics::ConnectionClosed();
  httpserver::Metrics::RequestStarted();
  httpserver::Metrics::ResponseSent(200);
  httpserver::Metrics::ResponseSent(404);
  httpserver::Metrics::IoError();
  const std::string output = httpserver::Metrics::Render(3, 2, 1);
  assert(output.find("httpserver_connections_active 1") != std::string::npos);
  assert(output.find("httpserver_responses_total{class=\"4xx\"} 1") != std::string::npos);
  assert(output.find("httpserver_rooms_active 3") != std::string::npos);
  return 0;
}
