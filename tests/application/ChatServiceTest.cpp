#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "httpserver/application/ChatService.h"

namespace {

class TestSession final : public httpserver::ConnectionSession {
 public:
  TestSession(std::uint64_t sessionId, std::string userId)
      : sessionId_(sessionId), userId_(std::move(userId)) {}

  std::uint64_t sessionId() const override { return sessionId_; }
  std::string_view userId() const override { return userId_; }
  void sendMessage(std::string_view message) override {
    received_.emplace_back(message);
  }

  const std::vector<std::string>& received() const { return received_; }

 private:
  std::uint64_t sessionId_;
  std::string userId_;
  std::vector<std::string> received_;
};

void TestRoomMembershipAndBroadcast() {
  httpserver::ChatService& service = httpserver::ChatService::Instance();
  service.ResetForTesting();

  const auto created = service.CreateRoom("alice", "engineering");
  assert(created.error == httpserver::ChatServiceError::kNone);
  assert(created.room.ownerId == "alice");
  assert(created.room.memberCount == 1);
  assert(created.room.joined);

  const auto roomsForBob = service.ListRooms("bob");
  assert(roomsForBob.empty());
  assert(service.JoinRoom("bob", created.room.id) ==
         httpserver::ChatServiceError::kNone);
  assert(service.JoinRoom("bob", created.room.id) ==
         httpserver::ChatServiceError::kAlreadyMember);
  const auto joinedRoomsForBob = service.ListRooms("bob");
  assert(joinedRoomsForBob.size() == 1);
  assert(joinedRoomsForBob.front().joined);

  const auto members = service.ListMembers("bob", created.room.id);
  assert(members.error == httpserver::ChatServiceError::kNone);
  assert((members.members == std::vector<std::string>{"alice", "bob"}));

  auto alice = std::make_shared<TestSession>(1, "alice");
  auto bob = std::make_shared<TestSession>(2, "bob");
  assert(service.AttachSession(created.room.id, alice) ==
         httpserver::ChatServiceError::kNone);
  assert(service.AttachSession(created.room.id, bob) ==
         httpserver::ChatServiceError::kNone);
  assert(service.Broadcast(created.room.id, "message", alice->sessionId()) ==
         httpserver::ChatServiceError::kNone);
  assert(alice->received().empty());
  assert((bob->received() == std::vector<std::string>{"message"}));

  service.DetachSession(alice->sessionId());
  assert(service.Broadcast(created.room.id, "ignored", alice->sessionId()) ==
         httpserver::ChatServiceError::kNotMember);
}

void TestMessageDispatchAndCursorHistory() {
  httpserver::ChatService& service = httpserver::ChatService::Instance();
  service.ResetForTesting();
  const auto created = service.CreateRoom("alice", "history");
  assert(created.error == httpserver::ChatServiceError::kNone);
  assert(service.JoinRoom("bob", created.room.id) ==
         httpserver::ChatServiceError::kNone);

  auto alice = std::make_shared<TestSession>(10, "alice");
  auto bob = std::make_shared<TestSession>(11, "bob");
  auto outsider = std::make_shared<TestSession>(12, "outsider");
  assert(service.AttachSession(created.room.id, alice) ==
         httpserver::ChatServiceError::kNone);
  assert(service.AttachSession(created.room.id, bob) ==
         httpserver::ChatServiceError::kNone);
  assert(service.AttachSession(created.room.id, outsider) ==
         httpserver::ChatServiceError::kNotMember);

  const auto forbidden = service.DispatchMessage(
      created.room.id, "outsider", "outside-1", "forbidden", 12);
  assert(forbidden.error == httpserver::ChatServiceError::kNotMember);
  const auto spoofed = service.DispatchMessage(
      created.room.id, "bob", "spoofed-1", "forbidden", alice->sessionId());
  assert(spoofed.error == httpserver::ChatServiceError::kNotMember);

  const auto first = service.DispatchMessage(
      created.room.id, "alice", "client-1", "first", alice->sessionId());
  const auto second = service.DispatchMessage(
      created.room.id, "alice", "client-2", "second", alice->sessionId());
  const auto third = service.DispatchMessage(
      created.room.id, "alice", "client-3", "third", alice->sessionId());
  assert(first.error == httpserver::ChatServiceError::kNone);
  assert(second.error == httpserver::ChatServiceError::kNone);
  assert(third.error == httpserver::ChatServiceError::kNone);
  assert(first.message.senderId == "alice");
  assert(first.recipients.size() == 1);

  const auto duplicate = service.DispatchMessage(
      created.room.id, "alice", "client-1", "first", alice->sessionId());
  assert(duplicate.error == httpserver::ChatServiceError::kDuplicateMessage);
  assert(duplicate.message.id == first.message.id);

  const auto page = service.ListMessages("alice", created.room.id, "", 2);
  assert(page.error == httpserver::ChatServiceError::kNone);
  assert(page.messages.size() == 2);
  assert(page.messages[0].id == second.message.id);
  assert(page.messages[1].id == third.message.id);
  assert(page.nextBefore == second.message.id);

  const auto older = service.ListMessages(
      "alice", created.room.id, *page.nextBefore, 2);
  assert(older.error == httpserver::ChatServiceError::kNone);
  assert(older.messages.size() == 1);
  assert(older.messages.front().id == first.message.id);
  assert(!older.nextBefore.has_value());
  assert(service.ListMessages("alice", created.room.id, "bad-cursor", 20)
             .error == httpserver::ChatServiceError::kInvalidCursor);
  assert(service.ListMessages("outsider", created.room.id, "", 20).error ==
         httpserver::ChatServiceError::kNotMember);

  assert(service.LeaveRoom("bob", created.room.id) ==
         httpserver::ChatServiceError::kNone);
  assert(service.DispatchMessage(created.room.id, "bob", "after-leave",
                                 "forbidden", bob->sessionId())
             .error == httpserver::ChatServiceError::kNotMember);
}

void TestLeaveRulesAndLegacyEnrollment() {
  httpserver::ChatService& service = httpserver::ChatService::Instance();
  service.ResetForTesting();
  const auto created = service.CreateRoom("owner", "general");
  assert(created.error == httpserver::ChatServiceError::kNone);

  assert(service.LeaveRoom("owner", created.room.id) ==
         httpserver::ChatServiceError::kOwnerCannotLeave);
  assert(service.LeaveRoom("missing", created.room.id) ==
         httpserver::ChatServiceError::kNotMember);

  auto legacy = std::make_shared<TestSession>(3, "legacy-user");
  assert(service.AttachSession(created.room.id, legacy, true) ==
         httpserver::ChatServiceError::kNone);
  const auto members = service.ListMembers("legacy-user", created.room.id);
  assert(members.error == httpserver::ChatServiceError::kNone);
  assert(members.members.size() == 2);
  assert(service.LeaveRoom("legacy-user", created.room.id) ==
         httpserver::ChatServiceError::kNone);
  assert(service.ListMembers("legacy-user", created.room.id).error ==
         httpserver::ChatServiceError::kNotMember);
}

void TestInputValidation() {
  httpserver::ChatService& service = httpserver::ChatService::Instance();
  service.ResetForTesting();
  assert(service.CreateRoom("", "room").error ==
         httpserver::ChatServiceError::kInvalidArgument);
  assert(service.CreateRoom("alice", "   ").error ==
         httpserver::ChatServiceError::kInvalidArgument);
  assert(service.JoinRoom("alice", "missing") ==
         httpserver::ChatServiceError::kRoomNotFound);
}

void TestConcurrentMembershipUpdates() {
  httpserver::ChatService& service = httpserver::ChatService::Instance();
  service.ResetForTesting();
  const auto created = service.CreateRoom("owner", "concurrency");
  assert(created.error == httpserver::ChatServiceError::kNone);

  std::vector<std::thread> workers;
  for (int index = 0; index < 32; ++index) {
    workers.emplace_back([&service, roomId = created.room.id, index] {
      assert(service.JoinRoom("member-" + std::to_string(index), roomId) ==
             httpserver::ChatServiceError::kNone);
    });
  }
  for (std::thread& worker : workers) worker.join();

  const auto members = service.ListMembers("owner", created.room.id);
  assert(members.error == httpserver::ChatServiceError::kNone);
  assert(members.members.size() == 33);
}

}  // namespace

int main() {
  TestRoomMembershipAndBroadcast();
  TestLeaveRulesAndLegacyEnrollment();
  TestInputValidation();
  TestMessageDispatchAndCursorHistory();
  TestConcurrentMembershipUpdates();
  httpserver::ChatService::Instance().ResetForTesting();
  return 0;
}
