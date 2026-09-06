#pragma once

#include "httpserver/application/RoomController.h"

namespace RoomController {
using httpserver::RoomController::handleCreateRoom;
using httpserver::RoomController::handleCreateRoomV1;
using httpserver::RoomController::handleGetRoomList;
using httpserver::RoomController::handleGetRoomListV1;
using httpserver::RoomController::handleGetRoomMessagesV1;
using httpserver::RoomController::handleGetRoomMembersV1;
using httpserver::RoomController::handleJoinRoomV1;
using httpserver::RoomController::handleLeaveRoomV1;
}
