# Suggestions — hivemind-esp32-client

1. **TLS with cert pinning** — Add `wss://` support via ESP-IDF TLS config. Pin server cert hash for identity verification.
2. **RSA handshake mode** — Implement asymmetric key exchange as alternative to password-based PBKDF2.
3. **OTA firmware update via HiveMind** — Use binary message channel to deliver firmware images from hub.
4. **Persistent key caching** — Cache derived session key in NVS to avoid PBKDF2 on reconnect when password unchanged.
5. **Dynamic buffer sizing** — Replace fixed 4096-byte buffers with configurable or heap-allocated buffers.
