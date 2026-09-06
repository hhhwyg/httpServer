#pragma once

#include <string_view>

#include "httpserver/application/ApplicationContext.h"

namespace httpserver::RoomController {

void handleCreateRoom(const ApplicationRequest& request,
                      std::string_view userId,
                      const ResponseSender& sendResponse);
void handleGetRoomList(std::string_view userId,
                       const ResponseSender& sendResponse);
void handleCreateRoomV1(const ApplicationRequest& request,
                        std::string_view userId,
                        const ResponseSender& sendResponse);
void handleGetRoomListV1(std::string_view userId,
                         const ResponseSender& sendResponse);
void handleJoinRoomV1(std::string_view roomId, std::string_view userId,
                      const ResponseSender& sendResponse);
void handleLeaveRoomV1(std::string_view roomId, std::string_view userId,
                       const ResponseSender& sendResponse);
void handleGetRoomMembersV1(std::string_view roomId,
                            std::string_view userId,
                            const ResponseSender& sendResponse);
void handleGetRoomMessagesV1(const ApplicationRequest& request,
                             std::string_view roomId,
                             std::string_view userId,
                             const ResponseSender& sendResponse);

}  // namespace httpserver::RoomController
