ALTER TABLE message_outbox
    ADD COLUMN lease_owner VARCHAR(128) NULL AFTER next_attempt_at;

ALTER TABLE message_outbox
    ADD COLUMN lease_until TIMESTAMP(3) NULL AFTER lease_owner;

ALTER TABLE message_outbox
    ADD INDEX idx_message_outbox_claim
        (published_at, next_attempt_at, lease_until, aggregate_id);

INSERT IGNORE INTO schema_migrations (version) VALUES ('002_outbox_leases');
