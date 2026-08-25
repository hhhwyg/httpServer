#include <cassert>
#include <cerrno>
#include <fcntl.h>
#include <memory>
#include <sys/socket.h>
#include <unistd.h>

#include "httpserver/transport/Channel.h"
#include "httpdata/HttpData.h"

namespace {

void makeSocketPair(int sockets[2]) {
  const int result = socketpair(AF_UNIX, SOCK_STREAM, 0, sockets);
  assert(result == 0);
}

void testHandlersDoNotCreateOwnershipCycle() {
  int sockets[2];
  makeSocketPair(sockets);

  std::shared_ptr<httpserver::Channel> channel;
  std::weak_ptr<HttpData> weakConnection;
  {
    auto connection = std::make_shared<HttpData>(nullptr, sockets[0]);
    connection->init();
    channel = connection->getChannel();
    weakConnection = connection;
  }

  assert(weakConnection.expired());
  channel->activate();
  channel->handleRead();
  close(sockets[1]);
}

void testCloseIsIdempotentAndReleasesFd() {
  int sockets[2];
  makeSocketPair(sockets);
  const int connectionFd = sockets[0];

  std::weak_ptr<HttpData> weakConnection;
  {
    auto connection = std::make_shared<HttpData>(nullptr, connectionFd);
    connection->init();
    weakConnection = connection;

    connection->handleClose();
    assert(connection->getFd() == -1);
    assert(fcntl(connectionFd, F_GETFD) == -1);
    assert(errno == EBADF);

    connection->handleClose();
  }

  assert(weakConnection.expired());
  close(sockets[1]);
}

}  // namespace

int main() {
  testHandlersDoNotCreateOwnershipCycle();
  testCloseIsIdempotentAndReleasesFd();
  return 0;
}
