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
  httpserver::Metrics::OutputBufferHighWater();
  httpserver::Metrics::OutputMessageDropped();
  httpserver::Metrics::SlowConsumerClosed();
  httpserver::Metrics::HttpRequestCompleted("GET", "/api/v1/rooms/42/messages",
                                             503, 12000);
  httpserver::Metrics::WebSocketConnectionOpened();
  httpserver::Metrics::WebSocketMessage("message", true);
  httpserver::Metrics::WebSocketBroadcast(800);
  const std::string output = httpserver::Metrics::Render(3, 2, 1);
  assert(output.find("httpserver_connections_active 1") != std::string::npos);
  assert(output.find("httpserver_responses_total{class=\"4xx\"} 1") != std::string::npos);
  assert(output.find("httpserver_rooms_active 3") != std::string::npos);
  assert(output.find("httpserver_output_buffer_high_water_total 1") != std::string::npos);
  assert(output.find("httpserver_slow_consumer_closed_total 1") != std::string::npos);
  assert(output.find("httpserver_http_requests_total{method=\"GET\",route=\"/api/v1/rooms/:id\",status_class=\"5xx\"} 1") != std::string::npos);
  assert(output.find("httpserver_http_request_duration_seconds_bucket{method=\"GET\",route=\"/api/v1/rooms/:id\",le=\"0.025\"} 1") != std::string::npos);
  assert(output.find("httpserver_websocket_connections 1") != std::string::npos);
  assert(output.find("httpserver_websocket_messages_total{type=\"message\",result=\"accepted\"} 1") != std::string::npos);
  return 0;
}
