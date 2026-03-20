# hivemind-esp32-client

ESP-IDF component implementing a HiveMind WebSocket satellite client for ESP32. Handles password-based handshake, AEAD encryption, bus messaging, and binary audio transport.

## Architecture

```
WebSocket events -> Protocol FSM -> Crypto layer -> Application callbacks
```

1. `esp_websocket_client` delivers frames to the internal WS event handler (`hivemind.c`)
2. During handshake, frames route through `hm_protocol_handle_message` — `hivemind_protocol.c:75`
3. In READY state, messages encrypt/decrypt via `hm_crypto_encrypt_json_hex` / `hm_crypto_decrypt_json_hex` — `hivemind_crypto.c`
4. Binary frames encode/decode via `hm_binary_encode` / `hm_binary_decode` — `hivemind_binary.c`

## Dependencies

- **mbedTLS** (ESP-IDF built-in, HW-accelerated AES on ESP32)
- **cJSON** (ESP-IDF built-in)
- **esp_websocket_client** (ESP-IDF component)

## Public API

### Client lifecycle — `include/hivemind.h`

| Function | Line | Description |
|----------|------|-------------|
| `hm_client_init` | 57 | Allocate and configure client |
| `hm_client_free` | 62 | Free client and resources |
| `hm_client_connect` | 87 | Start non-blocking WebSocket connection |
| `hm_client_disconnect` | 92 | Disconnect from hub |
| `hm_client_get_state` | 132 | Query current FSM state |
| `hm_client_set_bus_cb` | 67 | Register bus message callback |
| `hm_client_set_binary_cb` | 72 | Register binary data callback |
| `hm_client_set_state_cb` | 77 | Register state change callback |
| `hm_send_utterance` | 103 | Send text utterance (convenience) |
| `hm_send_bus_message` | 114 | Send generic bus message |
| `hm_send_binary` | 126 | Send binary data (audio, etc.) |

### Crypto — `include/hivemind_crypto.h`

| Function | Line | Description |
|----------|------|-------------|
| `hm_crypto_generate_hsub` | 50 | Generate 48-char hsub token |
| `hm_crypto_extract_iv` | 60 | Extract 8-byte IV from peer hsub |
| `hm_crypto_derive_key` | 73 | PBKDF2-HMAC-SHA256 key derivation (100k iterations) |
| `hm_crypto_encrypt_json_hex` | 90 | Encrypt to JSON-hex envelope |
| `hm_crypto_decrypt_json_hex` | 104 | Decrypt JSON-hex envelope |
| `hm_crypto_encrypt_binary` | 120 | Encrypt to binary frame (nonce+ct+tag) |
| `hm_crypto_decrypt_binary` | 135 | Decrypt binary frame |

### Protocol FSM — `include/hivemind_protocol.h`

| Function | Line | Description |
|----------|------|-------------|
| `hm_protocol_init` | 62 | Initialize protocol context |
| `hm_protocol_handle_message` | 75 | Process handshake message, advance FSM |
| `hm_protocol_encrypt_message` | 116 | Encrypt+envelope for sending |
| `hm_protocol_decrypt_message` | 130 | Decrypt+parse received message |

### Binary codec — `include/hivemind_binary.h`

| Function | Line | Description |
|----------|------|-------------|
| `hm_binary_encode` | 77 | Encode V1 bitstring frame |
| `hm_binary_decode` | 93 | Decode V1 bitstring frame |

## Handshake FSM states — `hivemind_protocol.h:20`

`DISCONNECTED` -> `CONNECTING` -> `HELLO_RECEIVED` -> `HANDSHAKE_SENT` -> `KEY_DERIVED` -> `READY`

## Key types

- `hm_cipher_t` — `hivemind_crypto.h:19`: `HM_CIPHER_AES_GCM`, `HM_CIPHER_CHACHA20_POLY1305`
- `hm_msg_type_t` — `hivemind_binary.h:22`: 14 message types (0-13)
- `hm_bin_type_t` — `hivemind_binary.h:40`: 7 binary payload types (0-6)
- `hm_state_t` — `hivemind_protocol.h:20`: 6 FSM states
