#include <arpa/inet.h>
#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cerrno>
#include <dirent.h>
#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <string_view>
#include <thread>
#include <vector>
#include <cstdint>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

constexpr std::string_view kRequest =
    "GET /ping HTTP/1.1\r\nHost: localhost\r\n"
    "Connection: keep-alive\r\n\r\n";
constexpr std::string_view kResponsePrefix = "HTTP/1.1 200 OK\r\n";
constexpr std::size_t kResponseBytes = 114;
constexpr std::size_t kLatencyBuckets = 4097;
constexpr std::uint64_t kLatencyBucketWidthUs = 25;

struct Connection {
  int fd = -1;
  std::size_t offset = 0;
  bool writing = true;
  bool timingStarted = false;
  std::chrono::steady_clock::time_point requestStarted{};
  char response[kResponseBytes]{};
};

struct WorkerStats {
  std::uint64_t completed = 0;
  std::uint64_t errors = 0;
  std::array<std::uint64_t, kLatencyBuckets> latencyBuckets{};
};

struct ProcessMetrics {
  bool valid = false;
  double cpuSeconds = 0.0;
  std::uint64_t rssKb = 0;
  std::uint64_t contextSwitches = 0;
};

std::uint64_t taskCpuTicks(const std::string& path) {
  std::ifstream statFile(path);
  std::string line;
  if (!std::getline(statFile, line)) return 0;
  const std::size_t closingName = line.rfind(')');
  if (closingName == std::string::npos) return 0;

  std::istringstream fields(line.substr(closingName + 2));
  std::string value;
  std::vector<std::string> values;
  while (fields >> value) values.push_back(value);
  if (values.size() <= 12) return 0;
  return std::stoull(values[11]) + std::stoull(values[12]);
}

ProcessMetrics readProcessMetrics(int pid) {
  if (pid <= 0) return {};
  ProcessMetrics metrics;
  const std::string taskPath = "/proc/" + std::to_string(pid) + "/task";
  DIR* directory = opendir(taskPath.c_str());
  if (directory == nullptr) return {};

  std::uint64_t cpuTicks = 0;
  while (dirent* entry = readdir(directory)) {
    if (entry->d_name[0] == '.') continue;
    bool numeric = true;
    for (const char* cursor = entry->d_name; *cursor != '\0'; ++cursor) {
      if (*cursor < '0' || *cursor > '9') numeric = false;
    }
    if (numeric) {
      cpuTicks += taskCpuTicks(taskPath + "/" + entry->d_name + "/stat");
    }
  }
  closedir(directory);

  std::ifstream statusFile("/proc/" + std::to_string(pid) + "/status");
  std::string line;
  while (std::getline(statusFile, line)) {
    std::istringstream fields(line);
    std::string key;
    std::uint64_t value = 0;
    if (!(fields >> key >> value)) continue;
    if (key == "VmRSS:") metrics.rssKb = value;
    if (key == "voluntary_ctxt_switches:" ||
        key == "nonvoluntary_ctxt_switches:") {
      metrics.contextSwitches += value;
    }
  }

  const long ticksPerSecond = sysconf(_SC_CLK_TCK);
  if (ticksPerSecond <= 0) return {};
  metrics.valid = true;
  metrics.cpuSeconds = static_cast<double>(cpuTicks) / ticksPerSecond;
  return metrics;
}

void recordLatency(WorkerStats& stats,
                   std::chrono::steady_clock::duration latency) {
  const auto micros = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(latency).count());
  const std::size_t bucket = std::min<std::size_t>(
      micros / kLatencyBucketWidthUs, kLatencyBuckets - 1);
  ++stats.latencyBuckets[bucket];
}

std::uint64_t latencyPercentile(const std::vector<WorkerStats>& stats,
                                std::uint64_t total, double fraction) {
  if (total == 0) return 0;
  const std::uint64_t target = static_cast<std::uint64_t>(
      std::ceil(static_cast<double>(total) * fraction));
  std::uint64_t seen = 0;
  for (std::size_t bucket = 0; bucket < kLatencyBuckets; ++bucket) {
    for (const WorkerStats& worker : stats) seen += worker.latencyBuckets[bucket];
    if (seen >= target) return bucket * kLatencyBucketWidthUs;
  }
  return (kLatencyBuckets - 1) * kLatencyBucketWidthUs;
}

bool addConnection(int poller, Connection& connection,
                   const sockaddr_in& address) {
  connection.offset = 0;
  connection.writing = true;
  connection.timingStarted = false;
  connection.fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (connection.fd < 0) return false;
  if (connect(connection.fd, reinterpret_cast<const sockaddr*>(&address),
              sizeof(address)) < 0 && errno != EINPROGRESS) {
    close(connection.fd);
    connection.fd = -1;
    return false;
  }
  epoll_event event{};
  event.data.ptr = &connection;
  event.events = EPOLLOUT | EPOLLERR | EPOLLHUP;
  if (epoll_ctl(poller, EPOLL_CTL_ADD, connection.fd, &event) < 0) {
    close(connection.fd);
    connection.fd = -1;
    return false;
  }
  return true;
}

bool modifyConnection(int poller, Connection& connection, std::uint32_t events) {
  epoll_event event{};
  event.data.ptr = &connection;
  event.events = events | EPOLLERR | EPOLLHUP;
  return epoll_ctl(poller, EPOLL_CTL_MOD, connection.fd, &event) == 0;
}

void closeConnection(int poller, Connection& connection) {
  if (connection.fd >= 0) {
    epoll_ctl(poller, EPOLL_CTL_DEL, connection.fd, nullptr);
    close(connection.fd);
    connection.fd = -1;
  }
}

void runWorker(const sockaddr_in& address, int connectionCount,
               std::chrono::steady_clock::time_point deadline,
               WorkerStats& stats) {
  const int poller = epoll_create1(EPOLL_CLOEXEC);
  if (poller < 0) return;
  std::vector<Connection> connections(static_cast<std::size_t>(connectionCount));
  for (Connection& connection : connections) {
    if (!addConnection(poller, connection, address)) {
      ++stats.errors;
    }
  }

  epoll_event events[256];
  while (std::chrono::steady_clock::now() < deadline) {
    const int count = epoll_wait(poller, events, 256, 10);
    if (count < 0) {
      if (errno == EINTR) continue;
      break;
    }
    for (int index = 0; index < count; ++index) {
      auto* connection = static_cast<Connection*>(events[index].data.ptr);
      if (connection->fd < 0) continue;
      const std::uint32_t ready = events[index].events;
      if (ready & (EPOLLERR | EPOLLHUP)) {
        ++stats.errors;
        closeConnection(poller, *connection);
        if (!addConnection(poller, *connection, address)) {
          ++stats.errors;
        }
        continue;
      }

      if (connection->writing && (ready & EPOLLOUT)) {
        if (!connection->timingStarted) {
          connection->requestStarted = std::chrono::steady_clock::now();
          connection->timingStarted = true;
        }
        int error = 0;
        socklen_t errorLength = sizeof(error);
        if (getsockopt(connection->fd, SOL_SOCKET, SO_ERROR, &error,
                       &errorLength) < 0 || error != 0) {
          ++stats.errors;
          closeConnection(poller, *connection);
          if (!addConnection(poller, *connection, address)) {
            ++stats.errors;
          }
          continue;
        }
        const ssize_t sent = send(connection->fd, kRequest.data() + connection->offset,
                                  kRequest.size() - connection->offset,
                                  MSG_NOSIGNAL);
        if (sent < 0 && errno != EAGAIN && errno != EINTR) {
          ++stats.errors;
          closeConnection(poller, *connection);
          if (!addConnection(poller, *connection, address)) {
            ++stats.errors;
          }
        } else if (sent > 0) {
          connection->offset += static_cast<std::size_t>(sent);
          if (connection->offset == kRequest.size()) {
            connection->offset = 0;
            connection->writing = false;
            modifyConnection(poller, *connection, EPOLLIN);
          }
        }
      }

      if (!connection->writing && (ready & EPOLLIN)) {
        const ssize_t received = recv(connection->fd,
                                      connection->response + connection->offset,
                                      kResponseBytes - connection->offset, 0);
        if (received < 0 && errno != EAGAIN && errno != EINTR) {
          ++stats.errors;
          closeConnection(poller, *connection);
          if (!addConnection(poller, *connection, address)) {
            ++stats.errors;
          }
        } else if (received == 0) {
          ++stats.errors;
          closeConnection(poller, *connection);
          if (!addConnection(poller, *connection, address)) {
            ++stats.errors;
          }
        } else if (received > 0) {
          connection->offset += static_cast<std::size_t>(received);
          if (connection->offset == kResponseBytes) {
            if (std::memcmp(connection->response, kResponsePrefix.data(),
                            kResponsePrefix.size()) == 0) {
              ++stats.completed;
              recordLatency(stats, std::chrono::steady_clock::now() -
                                      connection->requestStarted);
            } else {
              ++stats.errors;
            }
            connection->offset = 0;
            connection->writing = true;
            connection->timingStarted = false;
            modifyConnection(poller, *connection, EPOLLOUT);
          }
        }
      }
    }
  }

  for (Connection& connection : connections) closeConnection(poller, connection);
  close(poller);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 4 || argc > 6) {
    std::cerr << "usage: " << argv[0]
              << " PORT CONNECTIONS DURATION_SECONDS [THREADS] [SERVER_PID]\n";
    return 2;
  }
  const int port = std::stoi(argv[1]);
  const int totalConnections = std::stoi(argv[2]);
  const int durationSeconds = std::stoi(argv[3]);
  const int threadCount = argc >= 5 ? std::stoi(argv[4]) : 1;
  const int serverPid = argc == 6 ? std::stoi(argv[5]) : -1;
  if (port <= 0 || totalConnections <= 0 || durationSeconds <= 0 ||
      threadCount <= 0 || threadCount > totalConnections) {
    return 2;
  }

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(static_cast<std::uint16_t>(port));
  inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);

  const auto start = std::chrono::steady_clock::now();
  const ProcessMetrics before = readProcessMetrics(serverPid);
  const auto deadline = start + std::chrono::seconds(durationSeconds);
  std::vector<WorkerStats> workerStats(static_cast<std::size_t>(threadCount));
  std::vector<std::thread> workers;
  workers.reserve(static_cast<std::size_t>(threadCount));
  const int baseConnections = totalConnections / threadCount;
  const int remainder = totalConnections % threadCount;
  for (int index = 0; index < threadCount; ++index) {
    const int count = baseConnections + (index < remainder ? 1 : 0);
    workers.emplace_back(runWorker, std::cref(address), count, deadline,
                         std::ref(workerStats[static_cast<std::size_t>(index)]));
  }
  for (auto& worker : workers) worker.join();

  const double elapsed =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
          .count();
  const ProcessMetrics after = readProcessMetrics(serverPid);
  std::uint64_t completed = 0;
  std::uint64_t errors = 0;
  for (const WorkerStats& stats : workerStats) {
    completed += stats.completed;
    errors += stats.errors;
  }
  std::cout << "tool=cpp-epoll-http-benchmark\n"
            << "connections=" << totalConnections << "\n"
            << "threads=" << threadCount << "\n"
            << "duration_seconds=" << elapsed << "\n"
            << "successful_requests=" << completed << "\n"
            << "errors=" << errors << "\n"
            << "latency_p50_us_upper_bound="
            << latencyPercentile(workerStats, completed, 0.50) << "\n"
            << "latency_p95_us_upper_bound="
            << latencyPercentile(workerStats, completed, 0.95) << "\n"
            << "latency_p99_us_upper_bound="
            << latencyPercentile(workerStats, completed, 0.99) << "\n"
            << "requests_per_second="
            << static_cast<double>(completed) / elapsed << "\n";
  if (before.valid && after.valid) {
    const double cpuSeconds = after.cpuSeconds - before.cpuSeconds;
    const long logicalCpus = sysconf(_SC_NPROCESSORS_ONLN);
    std::cout << "server_cpu_seconds=" << cpuSeconds << "\n"
              << "server_cpu_percent_one_core="
              << (cpuSeconds / elapsed) * 100.0 << "\n"
              << "server_cpu_percent_machine="
              << (logicalCpus > 0 ? (cpuSeconds / elapsed) * 100.0 / logicalCpus
                                  : 0.0) << "\n"
              << "server_rss_before_kb=" << before.rssKb << "\n"
              << "server_rss_after_kb=" << after.rssKb << "\n"
              << "server_context_switches_delta="
              << (after.contextSwitches - before.contextSwitches) << "\n";
  }
}
