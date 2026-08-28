#include"ChatManager.h"

#include <utility>


std::string ChatManager::generateUniqueId() {
    // 使用 random_device 产生种子，mt19937 作为随机数引擎，并且加 thread_local 保证线程安全
    thread_local static std::random_device rd;
    thread_local static std::mt19937 gen(rd());
    // 定义 8 位数的范围：10,000,000 到 99,999,999
    std::uniform_int_distribution<> dis(10000000, 99999999);

    std::string newId;
    while (true) {
        newId = std::to_string(dis(gen));
        // 确保这个 ID 在当前 map 中不存在
        if (rooms_.find(newId) == rooms_.end()) {
            break;
        }
    }
    return newId;
}



std::shared_ptr<ChatRoom> ChatManager::getOrCreateRoom(std::string name) {
    std::lock_guard<std::mutex> lock(mtx_);
    std::string newRoomId=this->generateUniqueId();
    auto room = std::make_shared<ChatRoom>(newRoomId,name);
    rooms_[newRoomId] = room;
    return room;
}


bool ChatManager::joinRoom(
    const std::string& roomId,
    std::shared_ptr<httpserver::ConnectionSession> user) {
        if (!user) return false;
        std::shared_ptr<ChatRoom> room;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            const auto it = rooms_.find(roomId);
            if (it == rooms_.end()) return false;
            room = it->second;
        }
        room->join(std::move(user));
        return true;
    }


bool ChatManager::broadcastToRoom(const std::string& roomId, const std::string& msg,
                                  int senderFd) {
    std::shared_ptr<ChatRoom> room;
    {
        std::lock_guard<std::mutex> lock(mtx_); // 保护 rooms_ 字典
        const auto it = rooms_.find(roomId);
        if (it == rooms_.end()) return false;
        room = it->second;
    }
    if (!room->contains(senderFd)) return false;
    room->broadcast(msg, senderFd);
    return true;
}

bool ChatManager::isMember(const std::string& roomId, int fd) {
    std::shared_ptr<ChatRoom> room;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        const auto it = rooms_.find(roomId);
        if (it == rooms_.end()) return false;
        room = it->second;
    }
    return room->contains(fd);
}

void ChatManager::leaveUser(int fd) {
    std::vector<std::shared_ptr<ChatRoom>> rooms;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        rooms.reserve(rooms_.size());
        for (const auto& [id, room] : rooms_) {
            rooms.push_back(room);
        }
    }
    for (const auto& room : rooms) {
        room->leave(fd);
    }
}

std::vector<std::shared_ptr<ChatRoom>> ChatManager::getAllRooms() {
    std::lock_guard<std::mutex> lock(mtx_);
    std::vector<std::shared_ptr<ChatRoom>> allRooms;
    // 预分配空间提高效率
    allRooms.reserve(rooms_.size());
    // 3. 遍历 unordered_map，将其中的智能指针存入 vector
    // pair.first 是 roomId, pair.second 是 shared_ptr<ChatRoom>
    for (auto& pair : rooms_) {
        allRooms.push_back(pair.second);
    }
    return allRooms;
}
