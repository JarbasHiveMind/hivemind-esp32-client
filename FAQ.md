# FAQ — hivemind-esp32-client

**Q: What ciphers are supported?**
A: AES-256-GCM and ChaCha20-Poly1305. Negotiated during handshake via `ciphers` field in SHAKE message. Enum: `hm_cipher_t` — `hivemind_crypto.h:19`.

**Q: How long does PBKDF2 key derivation take on ESP32?**
A: 10-30 seconds (100,000 iterations, `HM_PBKDF2_ITERATIONS` — `hivemind_crypto.h:31`). One-time cost per connection. AES is HW-accelerated via mbedTLS on ESP32.

**Q: What is the minimum ESP-IDF version?**
A: ESP-IDF 5.0+. Requires `esp_websocket_client`, mbedTLS, and cJSON components.

**Q: How do I add this as a component?**
A: Copy `components/hivemind/` into your project's `components/` directory, or use the ESP Component Registry.

**Q: What is the binary protocol format?**
A: V1 bitstring: `pad(1) + versioned(1) + [version(8)] + type(5) + compressed(1) + metalen(8) + meta + [bintype(4)] + payload`. See `hivemind_binary.h:1-8`.

**Q: What are the fixed buffer sizes?**
A: Protocol uses 4096-byte buffers for envelopes and plaintext (`hivemind_protocol.c:216,239,433`). May be insufficient for large messages.

**Q: Does it support TLS?**
A: Not yet. V1 scope uses unencrypted WebSocket with application-layer AEAD encryption.
