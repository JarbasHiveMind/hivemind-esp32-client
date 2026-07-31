# Configuration & Credentials Reference

The client is configured by filling an `hm_config_t` (declared in `components/hivemind/include/hivemind.h`) and passing it to `hm_client_init`.

## Hub credentials

| Field | Meaning |
| --- | --- |
| `host` | Hub hostname or IP address. |
| `port` | Hub port. Default `5678`. |
| `username` | The client name registered on the hub. |
| `access_key` | Access key from `hivemind-core add-client`. |
| `password` | Password from `hivemind-core add-client`, used to derive the session key. |
| `site_id` | A site identifier reported to the hub (for example `esp32-kitchen`). |

Register a credential on the hub before connecting:

```bash
hivemind-core add-client --name esp32 \
  --access-key "your-access-key" --password "your-password"
```

## Cipher and encoding

| Field | Values | Default |
| --- | --- | --- |
| `preferred_cipher` | `HM_CIPHER_AES_GCM`, `HM_CIPHER_CHACHA20_POLY1305` | `HM_CIPHER_AES_GCM` |
| `preferred_encoding` | `hm_encoding_t` values | `HM_ENCODING_JSON_HEX` |

On ESP32, AES-GCM is hardware-accelerated; ChaCha20-Poly1305 runs in software. The hub makes the final choice during the handshake.

## Reconnect

| Field | Meaning | Default |
| --- | --- | --- |
| `reconnect_ms` | Delay before reconnecting after a drop. `0` disables auto-reconnect. | `5000` |

## Supplying credentials in the examples

The bundled examples read Wi-Fi and HiveMind settings from the project configuration rather than hardcoding them:

- **Wi-Fi**: via ESP-IDF's *Example Connection Configuration* menu (the `example_connect()` helper), set with `idf.py menuconfig`.
- **HiveMind**: `CONFIG_EXAMPLE_HIVEMIND_HOST`, `CONFIG_EXAMPLE_HIVEMIND_KEY`, `CONFIG_EXAMPLE_HIVEMIND_PASSWORD`, and `CONFIG_HIVEMIND_RECONNECT_MS`, also set through `idf.py menuconfig`.

In your own application you may set these fields to string literals, values read from NVS, or any other source.

## Handshake and key derivation

On connect the client runs the HiveMind V1 handshake:

```
DISCONNECTED -> CONNECTING -> HELLO_RECEIVED -> HANDSHAKE_SENT -> KEY_DERIVED -> READY
```

The session key is derived with PBKDF2-HMAC-SHA256 (100k iterations) from `password`, mixed with the IVs exchanged during the handshake. At `HM_STATE_READY` all bus and binary traffic is encrypted with the negotiated cipher. The derivation is the slow part of the first connection on-device; the derived key is reused on reconnect.

## Buffer sizes

Protocol buffers are fixed-size: 4096 bytes, `server_pubkey` 512 bytes, `server_peer` / `server_node_id` 128 bytes. Messages larger than the buffer can truncate. Stream audio in small chunks rather than single large frames. The WebSocket receive buffer can be raised with `CONFIG_HIVEMIND_WS_BUFFER_SIZE` (see `sdkconfig.defaults`).

---
[← Getting Started](getting-started.md) · [Home](../README.md) · [Examples →](examples.md)
