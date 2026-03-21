# HiveMind ESP32 Client Examples

**Practical implementation examples for common HiveMind satellite use cases on ESP32.**

---

## 1. Text Satellite (Text Command + TTS Response)

This example connects to the hub, sends a "hello world" utterance upon connection, and logs any "speak" messages received (TTS).

```c
#include <string.h>
#include "esp_log.h"
#include "hivemind.h"

static const char *TAG = "text_sat";

// Callback for bus messages (e.g. TTS responses)
static void on_bus_message(hm_client_t *client, const char *type, cJSON *data, cJSON *context) {
    if (strcmp(type, "speak") == 0) {
        cJSON *utterance = cJSON_GetObjectItem(data, "utterance");
        if (cJSON_IsString(utterance)) {
            ESP_LOGI(TAG, "TTS response: %s", utterance->valuestring);
        }
    }
}

// Callback for state changes (e.g. handshake completion)
static void on_state_change(hm_client_t *client, hm_state_t state) {
    if (state == HM_STATE_READY) {
        ESP_LOGI(TAG, "Connected! Sending utterance...");
        hm_send_utterance(client, "what time is it?");
    }
}

void app_main(void) {
    // ... Wi-Fi initialization code ...

    hm_config_t config = {
        .host = "192.168.1.100",
        .port = 5678,
        .username = "esp32-user",
        .access_key = "...",
        .password = "...",
        .site_id = "living-room",
    };

    hm_client_t *client = NULL;
    hm_client_init(&client, &config);
    hm_client_set_bus_cb(client, on_bus_message);
    hm_client_set_state_cb(client, on_state_change);
    
    hm_client_connect(client);
}
```

---

## 2. Mic Satellite (Streaming Audio to Hub)

This example demonstrates how to stream raw PCM audio (e.g., from an I2S microphone) to the HiveMind hub for STT processing.

```c
#include "hivemind.h"
#include "driver/i2s.h"

#define I2S_READ_LEN 2048

void mic_task(void *param) {
    hm_client_t *client = (hm_client_t *)param;
    uint8_t *buf = malloc(I2S_READ_LEN);
    size_t bytes_read;

    while (1) {
        // Read from I2S mic
        i2s_read(I2S_NUM_0, buf, I2S_READ_LEN, &bytes_read, portMAX_DELAY);

        // Send binary audio frame if client is ready
        if (hm_client_get_state(client) == HM_STATE_READY) {
            hm_send_binary(client, HM_BIN_RAW_AUDIO, buf, bytes_read);
        }
    }
}

// In app_main, after initializing client:
xTaskCreate(mic_task, "mic_task", 4096, client, 5, NULL);
```

---

## 3. Handling TTS Audio (Speaker Satellite)

Register a binary callback to receive and play back TTS audio chunks from the hub.

```c
#include "hivemind.h"

static void on_binary(hm_client_t *client, hm_bin_type_t bin_type, const uint8_t *data, size_t len) {
    if (bin_type == HM_BIN_TTS_AUDIO) {
        // 'data' is raw 16kHz 16-bit Mono PCM
        // play_to_i2s_speaker(data, len);
    }
}

// In app_main:
hm_client_set_binary_cb(client, on_binary);
```

---

## 4. Custom Bus Message

Trigger a custom skill or event on the HiveMind hub.

```c
cJSON *data = cJSON_CreateObject();
cJSON_AddStringToObject(data, "action", "turn_on");
cJSON_AddNumberToObject(data, "brightness", 100);

hm_send_bus_message(client, "home.automation:light", data, NULL);

cJSON_Delete(data);
```

---

## 5. Voice PE Satellite (Full Hardware Satellite)

Full-featured satellite for the **Home Assistant Voice Preview Edition** board (ESP32-S3). On-device wake word + VAD with TTS playback and LED state feedback.

**Hardware**: XMOS Voice Kit (mic) + TI AIC3204 (speaker) + 12x WS2812 LED ring + center button + mute switch.

**Audio pipeline**: I2S mic → ESP-SR AFE (WakeNet9 "hi_esp" + VAD) → HiveMind STT streaming → hub → TTS playback.

**Source**: `examples/voice_pe_satellite/`

**Key files**:
- `main.c` — audio pipeline task, HiveMind callbacks, UI task
- `speech_detect.c/h` — ESP-SR AFE wrapper (WakeNet + VAD)
- `voice_pe_hw.h` — all pin definitions for Voice PE board
- `codec_init.c` — I2C bus, XMOS reset, AIC3204 DAC register sequence
- `i2s_mic.c` — 32-bit stereo input → 16-bit mono extraction
- `i2s_spk.c` — ring buffer + 16 kHz→48 kHz upsample + 32-bit stereo output
- `led_ring.c` — 6-state solid colors (idle/wake-detected/listening/speaking/error/muted)
- `button.c` — GPIO0 ISR debounce (manual override) + GPIO3 mute polling

**States**:

| State | Trigger | LEDs | Audio |
|-------|---------|------|-------|
| IDLE | Default | Dim white | WakeNet listening |
| WAKE_DETECTED | "hi_esp" heard | Cyan | STT session starts |
| LISTENING | Speech detected | Blue | Streaming to hub |
| SPEAKING | TTS received | Green | Playing response |
| MUTED | Mute switch | Orange | All mic processing off |
| ERROR | Disconnected | Red | — |

**Build**:
```bash
cd examples/voice_pe_satellite
idf.py set-target esp32s3
idf.py menuconfig   # Set WiFi + HiveMind host/key/password
idf.py build flash monitor
```

**Dependencies**: `espressif/esp-sr ^1.3.0` (auto-fetched via `idf_component.yml`). Requires 16 MB flash with custom partition table for WakeNet model storage.
