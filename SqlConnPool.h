#pragma once
#include <mysql/mysql.h>
#include <string>
#include <queue>
#include <mutex>
#include <semaphore.h>
#include <thread>
#include "base/Logging.h" 

class SqlConnPool {
public:
    static SqlConnPool* Instance();

    void Init(const char* host, int port,
              const char* user, const char* pwd, 
              const char* dbName, int connSize);

    MYSQL *GetConn();
    void FreeConn(MYSQL * conn);
    int GetFreeConnCount();
    void ClosePool();

private:
    SqlConnPool();
    ~SqlConnPool();

    int MAX_CONN_;
    int useCount_;
    int freeCount_;

    std::queue<MYSQL *> connQue_; // 连接队列
    std::mutex mtx_;              // 互斥锁 (保护队列)
    sem_t semId_;                 // 信号量 (通知是否有空闲连接)
};

class SqlConnRAII {
public:
    SqlConnRAII(MYSQL** sql, SqlConnPool *connpool) {
        assert(connpool);
        *sql = connpool->GetConn();
        sql_ = *sql;
        connpool_ = connpool;
    }
    
    ~SqlConnRAII() {
        if(sql_) { connpool_->FreeConn(sql_); }
    }
    
private:
    MYSQL *sql_;
    SqlConnPool* connpool_;
};
