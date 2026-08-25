#include <cassert>
#include <memory>
#include <sys/socket.h>
#include <unistd.h>

#include "ChatManager.h"
#include "HttpData.h"

int main() {
  int sockets[2]{};
  assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);

  auto user = std::make_shared<HttpData>(nullptr, sockets[0]);
  auto room = ChatManager::getInstance().getOrCreateRoom("membership-test");
  assert(!ChatManager::getInstance().isMember(room->roomId_, user->getFd()));
  assert(ChatManager::getInstance().joinRoom(room->roomId_, user));
  assert(ChatManager::getInstance().isMember(room->roomId_, user->getFd()));
  assert(!ChatManager::getInstance().broadcastToRoom(room->roomId_, "ignored", 9999));

  ChatManager::getInstance().leaveUser(user->getFd());
  assert(!ChatManager::getInstance().isMember(room->roomId_, user->getFd()));

  user.reset();
  close(sockets[1]);
  return 0;
}
