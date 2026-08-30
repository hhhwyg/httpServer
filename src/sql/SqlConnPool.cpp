#include "SqlConnPool.h"

#include <cerrno>
#include <vector>

SqlConnPool::SqlConnPool() = default;

SqlConnPool* SqlConnPool::Instance() {
  static SqlConnPool connPool;
  return &connPool;
}

bool SqlConnPool::Init(const char* host, int port, const char* user,
                       const char* pwd, const char* dbName, int connSize) {
  if (!host || !user || !pwd || !dbName || connSize <= 0) {
    return false;
  }

  std::lock_guard<std::mutex> locker(mtx_);
  if (initialized_) {
    return false;
  }

  std::vector<MYSQL*> created;
  created.reserve(connSize);
  for (int index = 0; index < connSize; ++index) {
    MYSQL* sql = mysql_init(nullptr);
    if (!sql) {
      break;
    }

    const unsigned int connectTimeoutSeconds = 3;
    mysql_options(sql, MYSQL_OPT_CONNECT_TIMEOUT, &connectTimeoutSeconds);
    if (!mysql_real_connect(sql, host, user, pwd, dbName, port, nullptr, 0)) {
      LOG << "MySQL connection failed: " << mysql_error(sql);
      mysql_close(sql);
      break;
    }
    created.push_back(sql);
  }

  if (static_cast<int>(created.size()) != connSize) {
    for (MYSQL* sql : created) {
      mysql_close(sql);
    }
    return false;
  }
  if (sem_init(&semId_, 0, static_cast<unsigned int>(connSize)) != 0) {
    for (MYSQL* sql : created) {
      mysql_close(sql);
    }
    LOG << "MySQL pool semaphore initialization failed";
    return false;
  }

  semaphoreInitialized_ = true;
  for (MYSQL* sql : created) {
    connQue_.push(sql);
  }
  MAX_CONN_ = connSize;
  freeCount_ = connSize;
  initialized_ = true;
  return true;
}

MYSQL* SqlConnPool::GetConn() {
  std::lock_guard<std::mutex> locker(mtx_);
  if (!initialized_ || sem_trywait(&semId_) != 0) {
    return nullptr;
  }
  if (connQue_.empty()) {
    sem_post(&semId_);
    return nullptr;
  }
  MYSQL* sql = connQue_.front();
  connQue_.pop();
  ++useCount_;
  --freeCount_;
  return sql;
}

void SqlConnPool::FreeConn(MYSQL* sql) {
  if (!sql) {
    return;
  }

  std::lock_guard<std::mutex> locker(mtx_);
  if (!initialized_) {
    mysql_close(sql);
    return;
  }
  connQue_.push(sql);
  --useCount_;
  ++freeCount_;
  sem_post(&semId_);
}

int SqlConnPool::GetFreeConnCount() {
  std::lock_guard<std::mutex> locker(mtx_);
  return freeCount_;
}

int SqlConnPool::GetUseConnCount() {
  std::lock_guard<std::mutex> locker(mtx_);
  return useCount_;
}

bool SqlConnPool::IsInitialized() const {
  std::lock_guard<std::mutex> locker(mtx_);
  return initialized_;
}

void SqlConnPool::ClosePool() {
  std::queue<MYSQL*> connections;
  {
    std::lock_guard<std::mutex> locker(mtx_);
    if (!initialized_) {
      return;
    }
    connections.swap(connQue_);
    initialized_ = false;
    MAX_CONN_ = 0;
    useCount_ = 0;
    freeCount_ = 0;
  }

  while (!connections.empty()) {
    mysql_close(connections.front());
    connections.pop();
  }
  if (semaphoreInitialized_) {
    sem_destroy(&semId_);
    semaphoreInitialized_ = false;
  }
}

SqlConnPool::~SqlConnPool() { ClosePool(); }
