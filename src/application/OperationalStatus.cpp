#include "httpserver/application/OperationalStatus.h"

#include "httpserver/application/ChatService.h"
#include "CryptoUtil.h"
#include "SqlConnPool.h"
#include "httpserver/application/Metrics.h"
#include "httpserver/application/UserRepository.h"

namespace httpserver {

bool OperationalStatus::ready() {
  return CryptoUtil::jwtEnabled() && GetUserRepository().available();
}

std::string OperationalStatus::prometheusMetrics() {
  SqlConnPool* pool = SqlConnPool::Instance();
  return Metrics::Render(static_cast<int>(ChatService::Instance().RoomCount()),
                         pool->GetFreeConnCount(), pool->GetUseConnCount());
}

}  // namespace httpserver
