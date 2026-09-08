CREATE TABLE IF NOT EXISTS schema_migrations (
    version VARCHAR(64) PRIMARY KEY,
    applied_at TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS users (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    username VARCHAR(64) NOT NULL,
    password_hash VARCHAR(255) NOT NULL,
    created_at TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
    PRIMARY KEY (id),
    UNIQUE KEY uq_users_username (username)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS rooms (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    name VARCHAR(128) NOT NULL,
    owner_id BIGINT UNSIGNED NOT NULL,
    created_at TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
    PRIMARY KEY (id),
    CONSTRAINT fk_rooms_owner FOREIGN KEY (owner_id) REFERENCES users(id)
        ON DELETE RESTRICT ON UPDATE RESTRICT
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS room_members (
    room_id BIGINT UNSIGNED NOT NULL,
    user_id BIGINT UNSIGNED NOT NULL,
    joined_at TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
    PRIMARY KEY (room_id, user_id),
    CONSTRAINT fk_room_members_room FOREIGN KEY (room_id) REFERENCES rooms(id)
        ON DELETE CASCADE ON UPDATE RESTRICT,
    CONSTRAINT fk_room_members_user FOREIGN KEY (user_id) REFERENCES users(id)
        ON DELETE RESTRICT ON UPDATE RESTRICT
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS messages (
    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
    room_id BIGINT UNSIGNED NOT NULL,
    sender_id BIGINT UNSIGNED NOT NULL,
    client_message_id VARCHAR(128) NOT NULL,
    content VARCHAR(4096) NOT NULL,
    created_at TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
    PRIMARY KEY (id),
    UNIQUE KEY uq_messages_sender_client (sender_id, client_message_id),
    KEY idx_messages_room_id (room_id, id),
    CONSTRAINT fk_messages_room FOREIGN KEY (room_id) REFERENCES rooms(id)
        ON DELETE RESTRICT ON UPDATE RESTRICT,
    CONSTRAINT fk_messages_sender FOREIGN KEY (sender_id) REFERENCES users(id)
        ON DELETE RESTRICT ON UPDATE RESTRICT
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS message_outbox (
    event_id CHAR(36) NOT NULL,
    aggregate_id BIGINT UNSIGNED NOT NULL,
    event_type VARCHAR(64) NOT NULL,
    payload JSON NOT NULL,
    attempts INT UNSIGNED NOT NULL DEFAULT 0,
    next_attempt_at TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
    published_at TIMESTAMP(3) NULL,
    created_at TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
    PRIMARY KEY (event_id),
    KEY idx_message_outbox_pending (published_at, next_attempt_at, aggregate_id),
    CONSTRAINT fk_message_outbox_message FOREIGN KEY (aggregate_id) REFERENCES messages(id)
        ON DELETE CASCADE ON UPDATE RESTRICT
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

INSERT IGNORE INTO schema_migrations (version) VALUES ('001_initial');
