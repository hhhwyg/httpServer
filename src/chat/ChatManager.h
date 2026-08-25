#pragma once
#include"ChatRoom.h"
#include"Util.h"
#include"vector"

class HttpData;

class ChatManager {
public:
    static ChatManager& getInstance() {
        static ChatManager instance;
        return instance;
    }
    bool joinRoom(const std::string& roomId, std::shared_ptr<HttpData> user);
    bool broadcastToRoom(const std::string& roomId, const std::string& msg,
                         int senderFd);
    bool isMember(const std::string& roomId, int fd);
    void leaveUser(int fd);
    std::vector<std::shared_ptr<ChatRoom>>getAllRooms();
    std::shared_ptr<ChatRoom> getOrCreateRoom(std::string name);
private:
    std::mutex mtx_;
    std::unordered_map<std::string, std::shared_ptr<ChatRoom>> rooms_;
    std::string generateUniqueId();
};
