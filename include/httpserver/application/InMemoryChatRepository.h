#pragma once

#include <memory>

#include "httpserver/domain/ChatRepositories.h"

namespace httpserver {

class InMemoryChatRepository final : public RoomRepository,
                                     public MembershipRepository,
                                     public MessageRepository {
 public:
  InMemoryChatRepository();
  ~InMemoryChatRepository() override;

  Room Create(std::string_view ownerId, std::string_view name) override;
  std::optional<Room> Find(std::string_view roomId) const override;
  std::vector<Room> ListForMember(std::string_view userId) const override;
  std::size_t Count() const override;

  bool Add(std::string_view roomId, std::string_view userId) override;
  bool Remove(std::string_view roomId, std::string_view userId) override;
  bool Contains(std::string_view roomId,
                std::string_view userId) const override;
  std::vector<RoomMember> List(std::string_view roomId) const override;
  std::size_t Count(std::string_view roomId) const override;

  MessageAppendResult AppendOrGet(std::string_view roomId,
                                  std::string_view senderId,
                                  std::string_view clientMessageId,
                                  std::string_view content) override;
  std::vector<Message> ListBefore(std::string_view roomId,
                                  std::uint64_t beforeId,
                                  std::size_t limit) const override;
  std::vector<Message> ListAfter(std::string_view roomId,
                                 std::uint64_t afterId,
                                 std::size_t limit) const override;

  void Reset();

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace httpserver
