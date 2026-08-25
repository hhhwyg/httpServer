#pragma once

#include <memory>

class HttpData;

namespace httpserver::RoomController {

void handleCreateRoom(std::shared_ptr<::HttpData> httpData);
void handleGetRoomList(std::shared_ptr<::HttpData> httpData);

}  // namespace httpserver::RoomController
