# hivemind-esp32-client

ESP-IDF (C) client library that connects an ESP32 to a [HiveMind](https://github.com/JarbasHiveMind/HiveMind-core) hub as an encrypted WebSocket **satellite**.

A satellite captures input on an edge device — text, audio, sensors — and forwards it to a central **hub** ([hivemind-core](https://github.com/JarbasHiveMind/HiveMind-core)), which runs the AI reasoning (intent parsing, skills, text-to-speech) and sends responses back. This component implements the satellite side in C: the HiveMind handshake, AEAD encryption, bus messaging, and binary audio transport, sized for an ESP32.

```
ESP32 (this component)  ⇄  HiveMind hub (hivemind-core)  ⇄  OVOS skills
```

## Features

- **Encrypted** — AES-256-GCM (hardware-accelerated on ESP32) or ChaCha20-Poly1305; session key derived from your password via PBKDF2-HMAC-SHA256.
- **Bus + binary transport** — send text utterances and arbitrary bus messages; stream raw audio and receive TTS audio over the binary channel.
- **Auto-reconnect** — reconnects after a drop, configurable via `reconnect_ms`.
- **Drop-in component** — built on `esp_websocket_client`, `mbedtls`, and `cjson` (all in ESP-IDF).

## Prerequisites

- **ESP-IDF 5.0+** on the build host.
- An **ESP32** (or ESP32-S3 for the Voice PE example) with Wi-Fi.
- A running **HiveMind hub** ([hivemind-core](https://github.com/JarbasHiveMind/HiveMind-core)) reachable on the same network.
- A **client credential** (username, access key, password) issued by the hub with `hivemind-core add-client`.

## Install

Drop `components/hivemind/` into your project's `components/` directory (or pull it via the ESP Component Registry). The component declares its ESP-IDF dependencies: `esp_websocket_client`, `mbedtls`, `cjson`, `esp_timer`, `esp_random`, `log`.

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

- [`examples/text_satellite/`](examples/text_satellite/) — text-only satellite; sends utterances and logs responses.
- [`examples/mic_satellite/`](examples/mic_satellite/) — audio satellite with INMP441 I2S mic input.
- [`examples/voice_pe_satellite/`](examples/voice_pe_satellite/) — full Home Assistant Voice PE (ESP32-S3) satellite: VAD/wake word, STT/TTS over HiveMind or OVOS HTTP, LED ring and button.

## Configuration

`hm_config_t` fields:

| Field | Description | Default |
| --- | --- | --- |
| `host` | Hub hostname or IP | — |
| `port` | Hub port | `5678` |
| `username` | Client name registered on the hub | — |
| `access_key` | Access key from `hivemind-core add-client` | — |
| `password` | Password used for key derivation | — |
| `site_id` | Site identifier (for example `esp32-kitchen`) | — |
| `preferred_cipher` | `HM_CIPHER_AES_GCM` or `HM_CIPHER_CHACHA20_POLY1305` | AES-GCM |
| `preferred_encoding` | Negotiation preference | `HM_ENCODING_JSON_HEX` |
| `reconnect_ms` | Auto-reconnect delay (0 disables) | `5000` |

In the examples, `host`, `access_key`, and `password` come from `idf.py menuconfig` (`CONFIG_EXAMPLE_HIVEMIND_*`) and Wi-Fi is configured the same way.

## Troubleshooting

- **WebSocket connection fails** — confirm Wi-Fi is up (the monitor logs the IP), the hub is listening, and `host:port` is reachable.
- **Handshake timeout** — verify `username`, `access_key`, and `password` match the credential registered on the hub (`hivemind-core list-clients`).
- **First connection takes 10-30 s** — PBKDF2 at 100k iterations is slow on-device; subsequent reconnects reuse the derived key and are fast.
- **Out of memory** — protocol buffers are fixed-size (4096 bytes); large messages can truncate. Stream audio in small chunks.

See [`docs/integration-testing.md`](docs/integration-testing.md) for more.

## Documentation

- [`docs/getting-started.md`](docs/getting-started.md) — prerequisites and first satellite.
- [`docs/configuration.md`](docs/configuration.md) — config and credentials reference.
- [`docs/index.md`](docs/index.md) — architecture and full public API.
- [`docs/examples.md`](docs/examples.md) — text, mic, TTS, custom-message, and Voice PE examples.
- [`docs/integration-testing.md`](docs/integration-testing.md) — building and testing against a live hub.

## License

Apache 2.0
