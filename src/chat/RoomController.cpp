#include "RoomController.h"
#include "httpdata/HttpData.h"
#include "json.hpp"
#include "ChatManager.h"
#include "ChatRoom.h"

using json = nlohmann::json;

namespace httpserver::RoomController {

void handleCreateRoom(std::shared_ptr<HttpData> httpData) {
    json root;
    try {
        root = json::parse(httpData->getInBuffer().peekAllAsString());
    } catch (...) {
        httpData->sendResponse(400, "application/json", "{\"ok\":false,\"msg\":\"Invalid JSON\"}");
        return;
    }

    if (!root.is_object() || !root.contains("name") ||
        !root["name"].is_string()) {
        httpData->sendResponse(400, "application/json",
                               "{\"ok\":false,\"msg\":\"Invalid room name\"}");
        return;
    }
    std::string roomName = root["name"].get<std::string>();
    if (roomName.empty() || roomName.size() > 128) {
        httpData->sendResponse(400, "application/json",
                               "{\"ok\":false,\"msg\":\"Invalid room name\"}");
        return;
    }
    
    // 在 ChatManager 中创建并获取新 ID
    auto newRoom = ChatManager::getInstance().getOrCreateRoom(roomName);
    
    json resp;
    resp["ok"] = true;
    resp["roomId"] = newRoom->roomId_;
    httpData->sendResponse(200, "application/json", resp.dump());
}

void handleGetRoomList(std::shared_ptr<HttpData> httpData) {
    // 从 ChatManager 获取所有房间
    auto rooms = ChatManager::getInstance().getAllRooms(); 
    
    json resp;
    resp["ok"] = true;
    resp["rooms"] = json::array();
    for (auto& room : rooms) {
        resp["rooms"].push_back({{"id", room->roomId_}, {"name", room->name_}});
    }
    // 发送 JSON 响应
    httpData->sendResponse(200, "application/json", resp.dump());
}

} // namespace httpserver::RoomController
