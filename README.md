# hivemind-esp32-client

ESP-IDF (C) client library that connects an ESP32 to a [HiveMind](https://github.com/JarbasHiveMind/HiveMind-core) hub as an encrypted WebSocket **satellite**.

A satellite captures input on an edge device, such as text, audio, or sensor data, and forwards it to a central **hub** ([hivemind-core](https://github.com/JarbasHiveMind/HiveMind-core)). The hub runs the AI reasoning (intent parsing, skills, text-to-speech) and sends responses back. This component implements the satellite side in C. It handles the HiveMind handshake, AEAD encryption, bus messaging, and binary audio transport, sized for an ESP32.

```
ESP32 (this component)  ⇄  HiveMind hub (hivemind-core)  ⇄  OVOS skills
```

## Features

- **Encrypted**: AES-256-GCM (hardware-accelerated on ESP32) or ChaCha20-Poly1305. The session key comes from your password via PBKDF2-HMAC-SHA256.
- **Protocol v3 (Noise)**: `Noise_XXpsk2`/`Noise_KKpsk0` over X25519 + ChaCha20-Poly1305 + SHA-256 with a provisioned PSK. This gives mutual authentication, forward secrecy, and replay-resistant transport. The client falls back to the legacy handshake on older hubs (see [Configuration](#configuration)).
- **Bus + binary transport**: send text utterances and arbitrary bus messages. Stream raw audio and receive TTS audio over the binary channel.
- **Auto-reconnect**: reconnects after a drop, configurable via `reconnect_ms`.
- **Drop-in component**: built on `mbedtls` and `json` (bundled in ESP-IDF) plus the
  `espressif/esp_websocket_client` managed component, declared in the component's
  `idf_component.yml` and pulled in automatically by the IDF component manager.

## Prerequisites

- **ESP-IDF 5.1+** on the build host (CI builds against v5.4).
- An **ESP32** (or ESP32-S3 for the Voice PE example) with Wi-Fi.
- A running **HiveMind hub** ([hivemind-core](https://github.com/JarbasHiveMind/HiveMind-core)) reachable on the same network.
- A **client credential** (username, access key, password) issued by the hub with `hivemind-core add-client`.

## Install

Drop `components/hivemind/` into your project's `components/` directory. The
component's `idf_component.yml` declares its managed dependency
(`espressif/esp_websocket_client`) and an `idf >= 5.1` floor. Its `CMakeLists.txt`
requires the bundled `mbedtls`, `json`, `esp_timer`, `esp_random`, and `log`
components. Run `idf.py reconfigure` (or just `idf.py build`) and the IDF component
manager fetches the managed dependency into `managed_components/`.

## Quickstart

**1. Register the satellite on the hub** (where `hivemind-core` is installed):

```bash
hivemind-core add-client --name esp32 \
  --access-key "your-access-key" --password "your-password"
```

Keep the access key and password.

**2. Build and flash the text-satellite example:**

```bash
cd examples/text_satellite
idf.py set-target esp32
idf.py menuconfig    # set Wi-Fi SSID/password and the HiveMind host/key/password
idf.py build flash monitor
```

**3. Watch it talk to the hub.** On connect the example sends an utterance and logs the hub's `speak` reply:

```
I (4210) text_sat: Connecting to 192.168.1.100:5678
I (9880) text_sat: Connected! Sending utterance...
I (10120) text_sat: TTS response: It is half past three.
```

## Minimal client code

```c
#include "hivemind.h"

static void on_bus_message(hm_client_t *c, const char *type, cJSON *data, cJSON *context) {
    if (strcmp(type, "speak") == 0) {
        cJSON *utt = cJSON_GetObjectItem(data, "utterance");
        if (cJSON_IsString(utt)) ESP_LOGI("app", "hub: %s", utt->valuestring);
    }
}

static void on_state_change(hm_client_t *c, hm_state_t state) {
    if (state == HM_STATE_READY) hm_send_utterance(c, "what time is it?");
}

void app_main(void) {
    // ... bring up Wi-Fi first ...
    hm_config_t config = {
        .host = "192.168.1.100",
        .port = 5678,
        .username = "esp32",
        .access_key = "your-access-key",
        .password = "your-password",
        .site_id = "esp32-kitchen",
        .preferred_cipher = HM_CIPHER_AES_GCM,
    };
    hm_client_t *client = NULL;
    hm_client_init(&client, &config);
    hm_client_set_bus_cb(client, on_bus_message);
    hm_client_set_state_cb(client, on_state_change);
    hm_client_connect(client);
}
```

## Examples

- [`examples/text_satellite/`](examples/text_satellite/): text-only satellite. Sends utterances and logs responses.
- [`examples/mic_satellite/`](examples/mic_satellite/): audio satellite with INMP441 I2S mic input.
- [`examples/voice_pe_satellite/`](examples/voice_pe_satellite/): full Home Assistant Voice PE (ESP32-S3) satellite with VAD/wake word, STT/TTS over HiveMind or OVOS HTTP, an LED ring, and a button.

## Configuration

`hm_config_t` fields:

| Field | Description | Default |
| --- | --- | --- |
| `host` | Hub hostname or IP | none |
| `port` | Hub port | `5678` |
| `username` | Client name registered on the hub | none |
| `access_key` | Access key from `hivemind-core add-client` | none |
| `password` | Password used for key derivation | none |
| `site_id` | Site identifier (for example `esp32-kitchen`) | none |
| `preferred_cipher` | `HM_CIPHER_AES_GCM` or `HM_CIPHER_CHACHA20_POLY1305` | AES-GCM |
| `preferred_encoding` | Negotiation preference | `HM_ENCODING_JSON_HEX` |
| `reconnect_ms` | Auto-reconnect delay (0 disables) | `5000` |
| `noise_psk_hex` | Provisioned 32-byte PSK (64 hex chars) enabling protocol v3 | disabled |
| `noise_static_key_hex` | Own X25519 static private key (64 hex chars) | generated at init |
| `noise_server_key_hex` | Server's X25519 static public key. Pins the server and enables `KKpsk0` | unpinned (TOFU) |

### Protocol v3 (Noise handshake)

When the hub advertises protocol version 3 and a PSK is provisioned, the
client runs a Noise handshake (`Noise_XXpsk2_25519_ChaChaPoly_SHA256`, or
`Noise_KKpsk0_25519_ChaChaPoly_SHA256` when the server static key is
provisioned) instead of the legacy hsub/PBKDF2 handshake. This gives mutual
authentication, forward secrecy, and replay-resistant transport encryption
with sequential nonces. Servers without v3 keep working. The client falls
back to the legacy handshake automatically.

The PSK is **derived on a capable host, never on-device** (argon2id is
infeasible on a microcontroller). Derive it once for the target hub and
flash it:

```bash
# on the hub (or any machine with hivemind-core):
hivemind-core derive-psk --password "your-password"   # 64 hex chars
```

```c
hm_config_t config = {
    /* ... */
    .noise_psk_hex = "aabbcc...64 hex chars...",       /* enables v3 */
    .noise_static_key_hex = "112233...64 hex chars...",/* stable device identity */
    .noise_server_key_hex = NULL,                      /* TOFU-pin on first connect */
};
```

On the first completed `XXpsk2` handshake the client pins the server's
static key for the lifetime of the client. A changed key aborts later
handshakes. Provision `noise_server_key_hex` to persist the pin across
reboots. The access key remains a clear-text admission identifier.

In the examples, `host`, `access_key`, and `password` come from `idf.py menuconfig` (`CONFIG_EXAMPLE_HIVEMIND_*`) and Wi-Fi is configured the same way.

## Troubleshooting

- **WebSocket connection fails**: confirm Wi-Fi is up (the monitor logs the IP), the hub is listening, and `host:port` is reachable.
- **Handshake timeout**: verify `username`, `access_key`, and `password` match the credential registered on the hub (`hivemind-core list-clients`).
- **First connection takes 10-30 s**: PBKDF2 at 100k iterations is slow on-device. Subsequent reconnects reuse the derived key and are fast.
- **Out of memory**: protocol buffers are fixed-size (4096 bytes). Large messages can truncate. Stream audio in small chunks.

See [`docs/integration-testing.md`](docs/integration-testing.md) for more.

## Testing

Two layers run in CI (`.github/workflows/tests.yml`):

- **Host unit tests**: the crypto layer, the binary V1 codec, the handshake FSM,
  and the Voice PE audio helpers compile natively and run under Unity. mbedTLS is
  built from source, pinned to the same 3.6.x line ESP-IDF bundles, so the host links
  the identical crypto API the device uses. The crypto and binary suites include
  cross-platform interop vectors. These vectors pin the wire format to HiveMind Protocol V1:
  PBKDF2-HMAC-SHA256 100k / XOR-salt key derivation, the 48-hex hSub, AES-256-GCM and
  ChaCha20-Poly1305 AEAD, the `ciphertext`/`tag`/`nonce` JSON envelope, and all seven
  field encodings. Build them locally with:

  ```bash
  cmake -B build test_host/
  cmake --build build
  ./build/test_host_runner
  ```

- **ESP-IDF firmware build**: every example is compiled against a current stable
  ESP-IDF for its target. CI cannot run a live, hardware-in-the-loop test against a
  running hub. The manual procedure is in
  [`docs/integration-testing.md`](docs/integration-testing.md).

## Documentation

- [`docs/getting-started.md`](docs/getting-started.md): prerequisites and first satellite.
- [`docs/configuration.md`](docs/configuration.md): config and credentials reference.
- [`docs/index.md`](docs/index.md): architecture and full public API.
- [`docs/examples.md`](docs/examples.md): text, mic, TTS, custom-message, and Voice PE examples.
- [`docs/integration-testing.md`](docs/integration-testing.md): building and testing against a live hub.

## License

Apache 2.0
