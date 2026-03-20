# Audit — hivemind-esp32-client

## Known Issues

| ID | Severity | Description | Location |
|----|----------|-------------|----------|
| ESP-001 | Low | PBKDF2 100k iterations takes 10-30s on ESP32. Acceptable for one-time connection setup. | `hivemind_crypto.h:31` (`HM_PBKDF2_ITERATIONS`) |
| ESP-002 | Medium | Fixed 4096-byte buffers in protocol may truncate large messages. | `hivemind_protocol.c:216,239,433` |
| ESP-003 | Medium | No TLS certificate validation. Application-layer AEAD provides confidentiality but not server identity verification. | V1 design scope |
| ESP-004 | Low | No zlib compression support. `compressed` flag parsed in binary codec but decompression not implemented. | `hivemind_binary.h:56` (`compressed` field) |
| ESP-005 | Low | `server_pubkey` buffer fixed at 512 bytes, `server_peer`/`server_node_id` at 128 bytes. Oversized payloads silently truncated. | `hivemind_protocol.h:40-42` |
