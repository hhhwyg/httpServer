#include "MySqlChatRepository.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "SqlConnPool.h"

namespace httpserver {
namespace {

class Statement {
 public:
  Statement(MYSQL* connection, const char* sql)
      : statement_(connection ? mysql_stmt_init(connection) : nullptr) {
    if (statement_ &&
        mysql_stmt_prepare(statement_, sql,
                           static_cast<unsigned long>(std::char_traits<char>::length(sql))) != 0) {
      mysql_stmt_close(statement_);
      statement_ = nullptr;
    }
  }
  ~Statement() {
    if (statement_) mysql_stmt_close(statement_);
  }

  MYSQL_STMT* get() const {
    return statement_;
  }

 private:
  MYSQL_STMT* statement_;
};

struct StringBind {
  explicit StringBind(std::string_view value) : value(value) {
    length = static_cast<unsigned long>(this->value.size());
    bind.buffer_type = MYSQL_TYPE_STRING;
    bind.buffer = const_cast<char*>(this->value.data());
    bind.buffer_length = length;
    bind.length = &length;
  }

  std::string_view value;
  unsigned long length = 0;
  MYSQL_BIND bind{};
};

struct UIntBind {
  explicit UIntBind(std::uint64_t value) : value(value) {
    bind.buffer_type = MYSQL_TYPE_LONGLONG;
    bind.buffer = &this->value;
    bind.is_unsigned = true;
  }

  std::uint64_t value;
  MYSQL_BIND bind{};
};

bool Execute(MYSQL_STMT* statement, MYSQL_BIND* parameters, unsigned long parameterCount) {
  return statement && (parameterCount == 0 || mysql_stmt_bind_param(statement, parameters) == 0) &&
         mysql_stmt_execute(statement) == 0;
}

bool ForceOutboxFailureForTesting() {
#ifndef NDEBUG
  const char* enabled = std::getenv("HTTPSERVER_TEST_FAIL_OUTBOX_INSERT");
  return enabled != nullptr && std::string_view(enabled) == "1";
#else
  return false;
#endif
}

std::optional<std::uint64_t> ParseId(std::string_view id) {
  std::uint64_t value = 0;
  const auto result = std::from_chars(id.data(), id.data() + id.size(), value);
  return result.ec == std::errc{} && result.ptr == id.data() + id.size() && value > 0
             ? std::optional<std::uint64_t>(value)
             : std::nullopt;
}

class Transaction {
 public:
  explicit Transaction(MYSQL* connection) : connection_(connection) {
    active_ = connection_ && mysql_autocommit(connection_, 0) == 0;
  }
  ~Transaction() {
    if (active_) {
      mysql_rollback(connection_);
      mysql_autocommit(connection_, 1);
    }
  }

  bool active() const {
    return active_;
  }
  bool Commit() {
    if (!active_ || mysql_commit(connection_) != 0 || mysql_autocommit(connection_, 1) != 0) {
      return false;
    }
    active_ = false;
    return true;
  }

 private:
  MYSQL* connection_ = nullptr;
  bool active_ = false;
};

std::optional<std::uint64_t> UserId(MYSQL* connection, std::string_view username) {
  Statement statement(connection, "SELECT id FROM users WHERE username = ? LIMIT 1");
  if (!statement.get()) return std::nullopt;
  StringBind usernameBind(username);
  if (!Execute(statement.get(), &usernameBind.bind, 1) ||
      mysql_stmt_store_result(statement.get()) != 0) {
    return std::nullopt;
  }
  std::uint64_t id = 0;
  MYSQL_BIND result{};
  result.buffer_type = MYSQL_TYPE_LONGLONG;
  result.buffer = &id;
  result.is_unsigned = true;
  if (mysql_stmt_bind_result(statement.get(), &result) != 0 ||
      mysql_stmt_fetch(statement.get()) != 0) {
    return std::nullopt;
  }
  return id;
}

bool MembershipExists(MYSQL* connection, std::uint64_t roomId, std::uint64_t userId) {
  Statement statement(connection,
                      "SELECT 1 FROM room_members WHERE room_id = ? AND user_id = ? LIMIT 1");
  if (!statement.get()) return false;
  UIntBind roomBind(roomId);
  UIntBind userBind(userId);
  MYSQL_BIND parameters[] = {roomBind.bind, userBind.bind};
  if (!Execute(statement.get(), parameters, 2) || mysql_stmt_store_result(statement.get()) != 0) {
    return false;
  }
  unsigned char exists = 0;
  MYSQL_BIND result{};
  result.buffer_type = MYSQL_TYPE_TINY;
  result.buffer = &exists;
  result.is_unsigned = true;
  return mysql_stmt_bind_result(statement.get(), &result) == 0 &&
         mysql_stmt_fetch(statement.get()) == 0;
}

struct MessageRow {
  std::uint64_t id = 0;
  std::uint64_t roomId = 0;
  std::array<char, 65> sender{};
  std::array<char, 129> clientMessageId{};
  std::array<char, 4097> content{};
  std::array<char, 32> createdAt{};
  unsigned long senderLength = 0;
  unsigned long clientMessageIdLength = 0;
  unsigned long contentLength = 0;
  unsigned long createdAtLength = 0;
};

std::optional<Message> FetchMessage(MYSQL* connection, std::uint64_t messageId) {
  constexpr char kSql[] =
      "SELECT m.id, m.room_id, u.username, m.client_message_id, m.content, "
      "CONCAT(DATE_FORMAT(m.created_at, '%Y-%m-%dT%H:%i:%s.'), "
      "LPAD(FLOOR(MICROSECOND(m.created_at) / 1000), 3, '0'), 'Z') "
      "FROM messages m JOIN users u ON u.id = m.sender_id WHERE m.id = ?";
  Statement statement(connection, kSql);
  if (!statement.get()) return std::nullopt;
  UIntBind messageBind(messageId);
  if (!Execute(statement.get(), &messageBind.bind, 1) ||
      mysql_stmt_store_result(statement.get()) != 0) {
    return std::nullopt;
  }
  MessageRow row;
  MYSQL_BIND result[6]{};
  result[0].buffer_type = MYSQL_TYPE_LONGLONG;
  result[0].buffer = &row.id;
  result[0].is_unsigned = true;
  result[1].buffer_type = MYSQL_TYPE_LONGLONG;
  result[1].buffer = &row.roomId;
  result[1].is_unsigned = true;
  result[2].buffer_type = MYSQL_TYPE_STRING;
  result[2].buffer = row.sender.data();
  result[2].buffer_length = row.sender.size();
  result[2].length = &row.senderLength;
  result[3].buffer_type = MYSQL_TYPE_STRING;
  result[3].buffer = row.clientMessageId.data();
  result[3].buffer_length = row.clientMessageId.size();
  result[3].length = &row.clientMessageIdLength;
  result[4].buffer_type = MYSQL_TYPE_STRING;
  result[4].buffer = row.content.data();
  result[4].buffer_length = row.content.size();
  result[4].length = &row.contentLength;
  result[5].buffer_type = MYSQL_TYPE_STRING;
  result[5].buffer = row.createdAt.data();
  result[5].buffer_length = row.createdAt.size();
  result[5].length = &row.createdAtLength;
  if (mysql_stmt_bind_result(statement.get(), result) != 0 ||
      mysql_stmt_fetch(statement.get()) != 0) {
    return std::nullopt;
  }
  return Message{std::to_string(row.id),
                 std::to_string(row.roomId),
                 std::string(row.sender.data(), row.senderLength),
                 std::string(row.clientMessageId.data(), row.clientMessageIdLength),
                 std::string(row.content.data(), row.contentLength),
                 std::string(row.createdAt.data(), row.createdAtLength)};
}

std::optional<Message> FetchMessageByClientId(MYSQL* connection, std::uint64_t senderId,
                                              std::string_view clientMessageId) {
  Statement statement(connection,
                      "SELECT id FROM messages WHERE sender_id = ? AND client_message_id = ? "
                      "LIMIT 1");
  if (!statement.get()) return std::nullopt;
  UIntBind senderBind(senderId);
  StringBind clientBind(clientMessageId);
  MYSQL_BIND parameters[] = {senderBind.bind, clientBind.bind};
  if (!Execute(statement.get(), parameters, 2) || mysql_stmt_store_result(statement.get()) != 0) {
    return std::nullopt;
  }
  std::uint64_t id = 0;
  MYSQL_BIND result{};
  result.buffer_type = MYSQL_TYPE_LONGLONG;
  result.buffer = &id;
  result.is_unsigned = true;
  if (mysql_stmt_bind_result(statement.get(), &result) != 0 ||
      mysql_stmt_fetch(statement.get()) != 0) {
    return std::nullopt;
  }
  return FetchMessage(connection, id);
}

class MySqlChatRepository final : public ChatRepository {
 public:
  bool available() const override {
    // Schema compatibility is checked once before the server accepts traffic.
    // Rechecking it per request would perform synchronous SQL in /readyz.
    return SqlConnPool::Instance()->IsInitialized();
  }

  Room Create(std::string_view ownerId, std::string_view name) override {
    MYSQL* connection = nullptr;
    SqlConnRAII lease(&connection, SqlConnPool::Instance());
    if (!connection) return {};
    Transaction transaction(connection);
    const auto owner = UserId(connection, ownerId);
    if (!transaction.active() || !owner.has_value()) return {};
    Statement createRoom(connection, "INSERT INTO rooms (name, owner_id) VALUES (?, ?)");
    if (!createRoom.get()) return {};
    StringBind nameBind(name);
    UIntBind ownerBind(*owner);
    MYSQL_BIND roomParameters[] = {nameBind.bind, ownerBind.bind};
    if (!Execute(createRoom.get(), roomParameters, 2)) return {};
    const std::uint64_t roomId = mysql_stmt_insert_id(createRoom.get());
    Statement addOwner(connection, "INSERT INTO room_members (room_id, user_id) VALUES (?, ?)");
    UIntBind roomBind(roomId);
    MYSQL_BIND memberParameters[] = {roomBind.bind, ownerBind.bind};
    if (!addOwner.get() || !Execute(addOwner.get(), memberParameters, 2) || !transaction.Commit()) {
      return {};
    }
    return {std::to_string(roomId), std::string(name), std::string(ownerId)};
  }

  std::optional<Room> Find(std::string_view roomId) const override {
    const auto parsedRoomId = ParseId(roomId);
    MYSQL* connection = nullptr;
    SqlConnRAII lease(&connection, SqlConnPool::Instance());
    if (!parsedRoomId || !connection) return std::nullopt;
    Statement statement(connection,
                        "SELECT r.id, r.name, u.username FROM rooms r "
                        "JOIN users u ON u.id = r.owner_id WHERE r.id = ?");
    if (!statement.get()) return std::nullopt;
    UIntBind roomBind(*parsedRoomId);
    if (!Execute(statement.get(), &roomBind.bind, 1) ||
        mysql_stmt_store_result(statement.get()) != 0)
      return std::nullopt;
    std::uint64_t id = 0;
    std::array<char, 129> name{};
    std::array<char, 65> owner{};
    unsigned long nameLength = 0;
    unsigned long ownerLength = 0;
    MYSQL_BIND result[3]{};
    result[0].buffer_type = MYSQL_TYPE_LONGLONG;
    result[0].buffer = &id;
    result[0].is_unsigned = true;
    result[1].buffer_type = MYSQL_TYPE_STRING;
    result[1].buffer = name.data();
    result[1].buffer_length = name.size();
    result[1].length = &nameLength;
    result[2].buffer_type = MYSQL_TYPE_STRING;
    result[2].buffer = owner.data();
    result[2].buffer_length = owner.size();
    result[2].length = &ownerLength;
    if (mysql_stmt_bind_result(statement.get(), result) != 0 ||
        mysql_stmt_fetch(statement.get()) != 0)
      return std::nullopt;
    return Room{std::to_string(id), std::string(name.data(), nameLength),
                std::string(owner.data(), ownerLength)};
  }

  std::vector<Room> ListForMember(std::string_view userId) const override {
    MYSQL* connection = nullptr;
    SqlConnRAII lease(&connection, SqlConnPool::Instance());
    const auto memberId = connection ? UserId(connection, userId) : std::nullopt;
    if (!memberId) return {};
    Statement statement(connection,
                        "SELECT r.id, r.name, owner.username FROM rooms r "
                        "JOIN room_members rm ON rm.room_id = r.id "
                        "JOIN users owner ON owner.id = r.owner_id "
                        "WHERE rm.user_id = ? ORDER BY r.id");
    UIntBind memberBind(*memberId);
    if (!statement.get() || !Execute(statement.get(), &memberBind.bind, 1) ||
        mysql_stmt_store_result(statement.get()) != 0)
      return {};
    std::vector<Room> rooms;
    std::uint64_t id = 0;
    std::array<char, 129> name{};
    std::array<char, 65> owner{};
    unsigned long nameLength = 0, ownerLength = 0;
    MYSQL_BIND result[3]{};
    result[0].buffer_type = MYSQL_TYPE_LONGLONG;
    result[0].buffer = &id;
    result[0].is_unsigned = true;
    result[1].buffer_type = MYSQL_TYPE_STRING;
    result[1].buffer = name.data();
    result[1].buffer_length = name.size();
    result[1].length = &nameLength;
    result[2].buffer_type = MYSQL_TYPE_STRING;
    result[2].buffer = owner.data();
    result[2].buffer_length = owner.size();
    result[2].length = &ownerLength;
    if (mysql_stmt_bind_result(statement.get(), result) != 0) return {};
    while (mysql_stmt_fetch(statement.get()) == 0) {
      rooms.push_back({std::to_string(id), std::string(name.data(), nameLength),
                       std::string(owner.data(), ownerLength)});
    }
    return rooms;
  }

  std::size_t Count() const override {
    MYSQL* connection = nullptr;
    SqlConnRAII lease(&connection, SqlConnPool::Instance());
    Statement statement(connection, "SELECT COUNT(*) FROM rooms");
    std::uint64_t count = 0;
    MYSQL_BIND result{};
    result.buffer_type = MYSQL_TYPE_LONGLONG;
    result.buffer = &count;
    result.is_unsigned = true;
    return statement.get() && Execute(statement.get(), nullptr, 0) &&
                   mysql_stmt_bind_result(statement.get(), &result) == 0 &&
                   mysql_stmt_fetch(statement.get()) == 0
               ? static_cast<std::size_t>(count)
               : 0;
  }

  bool Add(std::string_view roomId, std::string_view userId) override {
    const auto parsedRoomId = ParseId(roomId);
    MYSQL* connection = nullptr;
    SqlConnRAII lease(&connection, SqlConnPool::Instance());
    const auto memberId = connection ? UserId(connection, userId) : std::nullopt;
    if (!parsedRoomId || !memberId) return false;
    Statement statement(connection,
                        "INSERT IGNORE INTO room_members (room_id, user_id) VALUES (?, ?)");
    UIntBind roomBind(*parsedRoomId), memberBind(*memberId);
    MYSQL_BIND parameters[] = {roomBind.bind, memberBind.bind};
    return statement.get() && Execute(statement.get(), parameters, 2) &&
           mysql_stmt_affected_rows(statement.get()) == 1;
  }

  bool Remove(std::string_view roomId, std::string_view userId) override {
    const auto parsedRoomId = ParseId(roomId);
    MYSQL* connection = nullptr;
    SqlConnRAII lease(&connection, SqlConnPool::Instance());
    const auto memberId = connection ? UserId(connection, userId) : std::nullopt;
    if (!parsedRoomId || !memberId) return false;
    Statement statement(connection, "DELETE FROM room_members WHERE room_id = ? AND user_id = ?");
    UIntBind roomBind(*parsedRoomId), memberBind(*memberId);
    MYSQL_BIND parameters[] = {roomBind.bind, memberBind.bind};
    return statement.get() && Execute(statement.get(), parameters, 2) &&
           mysql_stmt_affected_rows(statement.get()) == 1;
  }

  bool Contains(std::string_view roomId, std::string_view userId) const override {
    const auto parsedRoomId = ParseId(roomId);
    MYSQL* connection = nullptr;
    SqlConnRAII lease(&connection, SqlConnPool::Instance());
    const auto memberId = connection ? UserId(connection, userId) : std::nullopt;
    return parsedRoomId && memberId && MembershipExists(connection, *parsedRoomId, *memberId);
  }

  std::vector<RoomMember> List(std::string_view roomId) const override {
    const auto parsedRoomId = ParseId(roomId);
    MYSQL* connection = nullptr;
    SqlConnRAII lease(&connection, SqlConnPool::Instance());
    if (!parsedRoomId || !connection) return {};
    Statement statement(connection,
                        "SELECT u.username FROM room_members rm "
                        "JOIN users u ON u.id = rm.user_id WHERE rm.room_id = ? "
                        "ORDER BY u.username");
    UIntBind roomBind(*parsedRoomId);
    if (!statement.get() || !Execute(statement.get(), &roomBind.bind, 1) ||
        mysql_stmt_store_result(statement.get()) != 0)
      return {};
    std::array<char, 65> username{};
    unsigned long length = 0;
    MYSQL_BIND result{};
    result.buffer_type = MYSQL_TYPE_STRING;
    result.buffer = username.data();
    result.buffer_length = username.size();
    result.length = &length;
    if (mysql_stmt_bind_result(statement.get(), &result) != 0) return {};
    std::vector<RoomMember> members;
    while (mysql_stmt_fetch(statement.get()) == 0) {
      members.push_back({std::string(roomId), std::string(username.data(), length)});
    }
    return members;
  }

  std::size_t Count(std::string_view roomId) const override {
    const auto parsedRoomId = ParseId(roomId);
    MYSQL* connection = nullptr;
    SqlConnRAII lease(&connection, SqlConnPool::Instance());
    if (!parsedRoomId || !connection) return 0;
    Statement statement(connection, "SELECT COUNT(*) FROM room_members WHERE room_id = ?");
    UIntBind roomBind(*parsedRoomId);
    std::uint64_t count = 0;
    MYSQL_BIND result{};
    result.buffer_type = MYSQL_TYPE_LONGLONG;
    result.buffer = &count;
    result.is_unsigned = true;
    return statement.get() && Execute(statement.get(), &roomBind.bind, 1) &&
                   mysql_stmt_bind_result(statement.get(), &result) == 0 &&
                   mysql_stmt_fetch(statement.get()) == 0
               ? static_cast<std::size_t>(count)
               : 0;
  }

  MessageAppendResult AppendOrGet(std::string_view roomId, std::string_view sender,
                                  std::string_view clientMessageId,
                                  std::string_view content) override {
    const auto parsedRoomId = ParseId(roomId);
    MYSQL* connection = nullptr;
    SqlConnRAII lease(&connection, SqlConnPool::Instance());
    const auto senderId = connection ? UserId(connection, sender) : std::nullopt;
    if (!parsedRoomId || !senderId || !connection) {
      return {ChatServiceError::kDependencyUnavailable, {}, false};
    }
    Transaction transaction(connection);
    if (!transaction.active()) return {ChatServiceError::kDependencyUnavailable, {}, false};
    if (!MembershipExists(connection, *parsedRoomId, *senderId)) {
      return {ChatServiceError::kNotMember, {}, false};
    }
    Statement insert(connection,
                     "INSERT INTO messages (room_id, sender_id, client_message_id, content) "
                     "VALUES (?, ?, ?, ?)");
    UIntBind roomBind(*parsedRoomId), senderBind(*senderId);
    StringBind clientBind(clientMessageId), contentBind(content);
    MYSQL_BIND parameters[] = {roomBind.bind, senderBind.bind, clientBind.bind, contentBind.bind};
    if (!insert.get() || !Execute(insert.get(), parameters, 4)) {
      if (insert.get() && mysql_stmt_errno(insert.get()) == 1062) {
        const auto existing = FetchMessageByClientId(connection, *senderId, clientMessageId);
        if (existing && transaction.Commit()) return {ChatServiceError::kNone, *existing, false};
      }
      return {ChatServiceError::kDependencyUnavailable, {}, false};
    }
    const std::uint64_t messageId = mysql_stmt_insert_id(insert.get());
    if (ForceOutboxFailureForTesting()) {
      return {ChatServiceError::kDependencyUnavailable, {}, false};
    }
    Statement outbox(connection,
                     "INSERT INTO message_outbox (event_id, aggregate_id, event_type, payload) "
                     "VALUES (UUID(), ?, 'message.created', "
                     "JSON_OBJECT('messageId', ?, 'roomId', ?, 'senderId', ?, "
                     "'clientMessageId', ?, 'content', ?, 'createdAt', "
                     "DATE_FORMAT(NOW(3), '%Y-%m-%dT%H:%i:%s.%fZ'), "
                     "'occurredAt', DATE_FORMAT(NOW(3), '%Y-%m-%dT%H:%i:%s.%fZ')))");
    UIntBind messageBind(messageId), outboxRoomBind(*parsedRoomId);
    StringBind outboxSenderBind(sender), outboxClientBind(clientMessageId),
        outboxContentBind(content);
    MYSQL_BIND outboxParameters[] = {messageBind.bind,      messageBind.bind,
                                     outboxRoomBind.bind,   outboxSenderBind.bind,
                                     outboxClientBind.bind, outboxContentBind.bind};
    if (!outbox.get() || !Execute(outbox.get(), outboxParameters, 6)) {
      return {ChatServiceError::kDependencyUnavailable, {}, false};
    }
    const auto message = FetchMessage(connection, messageId);
    if (!message || !transaction.Commit()) {
      return {ChatServiceError::kDependencyUnavailable, {}, false};
    }
    return {ChatServiceError::kNone, *message, true};
  }

  std::vector<Message> ListBefore(std::string_view roomId, std::uint64_t before,
                                  std::size_t limit) const override {
    return ListMessages(roomId, before, limit, false);
  }
  std::vector<Message> ListAfter(std::string_view roomId, std::uint64_t after,
                                 std::size_t limit) const override {
    return ListMessages(roomId, after, limit, true);
  }

 private:
  std::vector<Message> ListMessages(std::string_view roomId, std::uint64_t cursor,
                                    std::size_t limit, bool after) const {
    const auto parsedRoomId = ParseId(roomId);
    MYSQL* connection = nullptr;
    SqlConnRAII lease(&connection, SqlConnPool::Instance());
    if (!parsedRoomId || !connection) return {};
    const char* sql =
        after ? "SELECT id FROM messages WHERE room_id = ? AND id > ? ORDER BY id ASC LIMIT ?"
              : "SELECT id FROM messages WHERE room_id = ? AND id < ? ORDER BY id DESC LIMIT ?";
    Statement statement(connection, sql);
    UIntBind roomBind(*parsedRoomId), cursorBind(cursor), limitBind(limit);
    MYSQL_BIND parameters[] = {roomBind.bind, cursorBind.bind, limitBind.bind};
    if (!statement.get() || !Execute(statement.get(), parameters, 3) ||
        mysql_stmt_store_result(statement.get()) != 0)
      return {};
    std::uint64_t id = 0;
    MYSQL_BIND result{};
    result.buffer_type = MYSQL_TYPE_LONGLONG;
    result.buffer = &id;
    result.is_unsigned = true;
    if (mysql_stmt_bind_result(statement.get(), &result) != 0) return {};
    std::vector<Message> messages;
    while (mysql_stmt_fetch(statement.get()) == 0) {
      const auto message = FetchMessage(connection, id);
      if (!message) return {};
      messages.push_back(*message);
    }
    if (!after) std::reverse(messages.begin(), messages.end());
    return messages;
  }
};

}  // namespace

std::unique_ptr<ChatRepository> CreateMySqlChatRepository() {
  return std::make_unique<MySqlChatRepository>();
}

bool MySqlChatSchemaCurrent() {
  MYSQL* connection = nullptr;
  SqlConnRAII lease(&connection, SqlConnPool::Instance());
  if (!connection) return false;
  Statement statement(connection,
                      "SELECT 1 FROM schema_migrations WHERE version = '002_outbox_leases'");
  if (!statement.get() || !Execute(statement.get(), nullptr, 0) ||
      mysql_stmt_store_result(statement.get()) != 0)
    return false;
  unsigned char exists = 0;
  MYSQL_BIND result{};
  result.buffer_type = MYSQL_TYPE_TINY;
  result.buffer = &exists;
  result.is_unsigned = true;
  return mysql_stmt_bind_result(statement.get(), &result) == 0 &&
         mysql_stmt_fetch(statement.get()) == 0;
}

}  // namespace httpserver
