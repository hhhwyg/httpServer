#include "Server.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <functional>
#include "Util.h"
#include "base/ObjectPool.h"
#include "HttpData.h"
#include "httpserver/base/Logging.h"
#include "httpserver/application/Metrics.h"

Server::Server(httpserver::EventLoop* loop, int threadNum, int port,
               httpserver::PollerBackend backend,
               httpserver::PollerConfig config)
    : loop_(loop),
      threadNum_(threadNum),
      eventLoopThreadPool_(
          new EventLoopThreadPool(loop_, threadNum, backend, config)),
      started_(false),
      acceptChannel_(new httpserver::Channel(loop, 0)),
      port_(port),
      listenFd_(socket_bind_listen(port_)),
      maxFds_(config.maxFds) {
  acceptChannel_->setFd(listenFd_);
  handle_for_sigpipe();
  if (setSocketNonBlocking(listenFd_) < 0) {
    perror("set socket non block failed");
    abort();
  }
  //std::cout << "[Server] listenFd_=" << listenFd_ << std::endl;

}

void Server::start() {
  eventLoopThreadPool_->start();
  // acceptChannel_->setEvents(EPOLLIN | EPOLLET | EPOLLONESHOT);
  acceptChannel_->setEvents(EPOLLIN | EPOLLET);
  acceptChannel_->setReadHandler(std::bind(&Server::handNewConn, this));
  acceptChannel_->setConnHandler(std::bind(&Server::handThisConn, this));
  loop_->addToPoller(acceptChannel_, 0);
  started_ = true;
}

void Server::stopAccepting() {
  if (!started_) return;
  started_ = false;
  if (acceptChannel_) {
    loop_->removeFromPoller(acceptChannel_);
    acceptChannel_->deactivate();
  }
  if (listenFd_ >= 0) {
    close(listenFd_);
    listenFd_ = -1;
  }
}

void Server::handNewConn() {
  if (!started_ || listenFd_ < 0) return;
  //std::cout << "[handNewConn] ENTER" << std::endl;
  struct sockaddr_in client_addr;
  memset(&client_addr, 0, sizeof(struct sockaddr_in));
  socklen_t client_addr_len = sizeof(client_addr);
  int accept_fd = 0;
  while ((accept_fd = accept(listenFd_, (struct sockaddr *)&client_addr,
                             &client_addr_len)) > 0) {
    httpserver::EventLoop* loop = eventLoopThreadPool_->getNextLoop();
    // LOG << "New connection from " << inet_ntoa(client_addr.sin_addr) << ":" << ntohs(client_addr.sin_port); // removed: fires per-connection, locks AsyncLogging mutex
    // cout << "new connection" << endl;
    // cout << inet_ntoa(client_addr.sin_addr) << endl;
    // cout << ntohs(client_addr.sin_port) << endl;
    /*
    // TCP的保活机制默认是关闭的
    int optval = 0;
    socklen_t len_optval = 4;
    getsockopt(accept_fd, SOL_SOCKET,  SO_KEEPALIVE, &optval, &len_optval);
    cout << "optval ==" << optval << endl;
    */
    // 限制服务器的最大并发连接数
    if (accept_fd >= maxFds_) {
      close(accept_fd);
      continue;
    }
    // 设为非阻塞模式
    if (setSocketNonBlocking(accept_fd) < 0) {
      LOG << "Set non block failed!";
      close(accept_fd);
      continue;
    }
    //关闭Nagle 算法  如果你发送的数据包很小（比如只有几个字节的 HTTP 头），TCP 不会立刻把它发出去。它会攒着，直到攒够了一个 MSS（最大报文段大小，通常 1460 字节），或者等到了上一个包的确认（ACK）回来。
    setSocketNodelay(accept_fd);
    // setSocketNoLinger(accept_fd);
    // 1. 获取对象池中的 HttpData 实例
    auto req_info = ObjectPool<HttpData>::Instance().acquire(loop, accept_fd);
    req_info->init();
    // 2. 极其重要的检查：确保 HttpData 和它内部的 Channel 都存在
    if (req_info && req_info->getChannel()) {
        httpserver::Metrics::ConnectionOpened();
        req_info->getChannel()->setOwner(req_info);
        loop->queueInLoop(std::bind(&HttpData::newEvent, req_info));
    } else {
        LOG << "HttpData or Channel creation failed!";
        close(accept_fd);
    }
  }
  // 3. 确保 acceptChannel_ 是有效的
  if (acceptChannel_) {
    acceptChannel_->setEvents(EPOLLIN | EPOLLET); 
  }
}
