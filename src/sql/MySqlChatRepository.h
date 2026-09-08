#pragma once

#include <memory>

#include "httpserver/domain/ChatRepositories.h"

namespace httpserver {

std::unique_ptr<ChatRepository> CreateMySqlChatRepository();
bool MySqlChatSchemaCurrent();

}  // namespace httpserver
