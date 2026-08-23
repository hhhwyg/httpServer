#pragma once
#include <memory>
#include "../httpdata/HttpData.h"

namespace RoomController {
    void handleCreateRoom(std::shared_ptr<HttpData> httpData);
    void handleGetRoomList(std::shared_ptr<HttpData> httpData);
}
