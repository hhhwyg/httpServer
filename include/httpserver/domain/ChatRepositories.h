#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "httpserver/domain/ChatDomain.h"

namespace httpserver {

class RoomRepository {
 public:
  virtual ~RoomRepository() = default;

  virtual Room Create(std::string_view ownerId, std::string_view name) = 0;
  virtual std::optional<Room> Find(std::string_view roomId) const = 0;
  virtual std::vector<Room> ListForMember(std::string_view userId) const = 0;
  virtual std::size_t Count() const = 0;
};

class MembershipRepository {
 public:
  virtual ~MembershipRepository() = default;

  virtual bool Add(std::string_view roomId, std::string_view userId) = 0;
  virtual bool Remove(std::string_view roomId, std::string_view userId) = 0;
  virtual bool Contains(std::string_view roomId,
                        std::string_view userId) const = 0;
  virtual std::vector<RoomMember> List(std::string_view roomId) const = 0;
  virtual std::size_t Count(std::string_view roomId) const = 0;
};

class MessageRepository {
 public:
  virtual ~MessageRepository() = default;

  // Atomically returns the existing logical message or appends a new one.
  // The idempotency key is (senderId, clientMessageId), not a connection ID.
  virtual MessageAppendResult AppendOrGet(
      std::string_view roomId, std::string_view senderId,
      std::string_view clientMessageId, std::string_view content) = 0;
  virtual std::vector<Message> ListBefore(std::string_view roomId,
                                          std::uint64_t beforeId,
                                          std::size_t limit) const = 0;
  virtual std::vector<Message> ListAfter(std::string_view roomId,
                                         std::uint64_t afterId,
                                         std::size_t limit) const = 0;
};

class ChatRepository : public RoomRepository,
                       public MembershipRepository,
                       public MessageRepository {
 public:
  ~ChatRepository() override = default;

  virtual bool available() const = 0;
};

}  // namespace httpserver
