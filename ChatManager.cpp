#include"ChatManager.h"


std::string ChatManager::generateUniqueId() {
    // 使用 random_device 产生种子，mt19937 作为随机数引擎
    static std::random_device rd;
    static std::mt19937 gen(rd());
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


void ChatManager::joinRoom(std::string roomId, std::shared_ptr<HttpData> user) {
        std::lock_guard<std::mutex> lock(mtx_);
        if (rooms_.find(roomId) == rooms_.end()) {
            return;
        }
        rooms_[roomId]->join(user);
    }


void ChatManager::broadcastToRoom(std::string roomId, const std::string& msg, int senderFd) {
    std::shared_ptr<ChatRoom> room;
    {
        std::lock_guard<std::mutex> lock(mtx_); // 保护 rooms_ 字典
        if (rooms_.find(roomId) == rooms_.end()) return;
        room = rooms_[roomId];
    }
    // 将具体的广播任务交给房间对象自己处理
    room->broadcast(msg, senderFd);
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