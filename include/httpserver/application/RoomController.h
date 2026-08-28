#pragma once

#include "httpserver/application/ApplicationContext.h"

namespace httpserver::RoomController {

void handleCreateRoom(const ApplicationRequest& request,
                      const ResponseSender& sendResponse);
void handleGetRoomList(const ApplicationRequest& request,
                       const ResponseSender& sendResponse);

}  // namespace httpserver::RoomController
