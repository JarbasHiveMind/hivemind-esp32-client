/**
 * @file main.c
 * @brief HiveMind Voice PE satellite — wake word + VAD audio satellite
 *        for the Home Assistant Voice Preview Edition hardware.
 *
 * Audio pipeline:
 *   1. I2S mic reads 16 kHz 16-bit mono from XMOS Voice Kit (continuous)
 *   2. ESP-SR AFE processes audio: WakeNet listens for "hi_esp" wake word
 *   3. On wake word detection → begin STT streaming to HiveMind hub
 *   4. VAD monitors speech; when silence detected → end STT session
 *   5. Hub processes STT → returns TTS audio → played through AIC3204 speaker
 *
 * The center button also works as manual push-to-talk override.
 * The mute switch disables all mic processing.
 */
#include <string.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"

#include "hivemind.h"
#include "voice_pe_hw.h"
#include "codec_init.h"
#include "i2s_mic.h"
#include "i2s_spk.h"
#include "led_ring.h"
#include "button.h"
#include "speech_detect.h"

static const char *TAG = "voice_pe";

static hm_client_t *s_client = NULL;
static volatile sat_state_t s_state = SAT_IDLE;
static volatile bool s_streaming = false;  /* Currently sending audio to hub. */

/* ── HiveMind callbacks ──────────────────────────────────────────── */

static void on_bus_message(hm_client_t *client, const char *type,
                            cJSON *data, cJSON *context)
{
    ESP_LOGI(TAG, "Bus: %s", type);

    if (strcmp(type, "speak") == 0) {
        cJSON *utt = cJSON_GetObjectItem(data, "utterance");
        if (utt && cJSON_IsString(utt)) {
            ESP_LOGI(TAG, "TTS: %s", utt->valuestring);
        }
    }

    /* Hub signals end of audio output. */
    if (strcmp(type, "recognizer_loop:audio_output_end") == 0) {
        if (s_state == SAT_SPEAKING) {
            s_state = SAT_IDLE;
        }
    }
}

static void on_binary(hm_client_t *client, hm_bin_type_t bin_type,
                       const uint8_t *data, size_t len)
{
    if (bin_type == HM_BIN_TTS_AUDIO) {
        ESP_LOGD(TAG, "TTS audio chunk: %zu bytes", len);
        s_state = SAT_SPEAKING;
        i2s_spk_push((const int16_t *)data, len / sizeof(int16_t));
    }
}

static void on_state_change(hm_client_t *client, hm_state_t state)
{
    ESP_LOGI(TAG, "HiveMind state: %d", state);

    if (state == HM_STATE_READY) {
        s_state = SAT_IDLE;
        ESP_LOGI(TAG, "Connected to hub — listening for wake word");
    } else if (state == HM_STATE_DISCONNECTED) {
        s_state = SAT_ERROR;
    }
}

/* ── Start/stop STT session helpers ──────────────────────────────── */

static void stt_session_start(void)
{
    if (s_streaming) {
        return;
    }
    s_streaming = true;
    s_state = SAT_LISTENING;
    hm_send_bus_message(s_client, "recognizer_loop:record_begin", NULL, NULL);
    ESP_LOGI(TAG, "STT session started");
}

static void stt_session_stop(void)
{
    if (!s_streaming) {
        return;
    }
    s_streaming = false;
    s_state = SAT_IDLE;
    hm_send_bus_message(s_client, "recognizer_loop:record_end", NULL, NULL);
    ESP_LOGI(TAG, "STT session ended");
}

/* ── Audio pipeline task ─────────────────────────────────────────── */

/**
 * Continuous audio pipeline:
 *   1. Read I2S mic → feed to ESP-SR AFE
 *   2. Fetch detection results from AFE
 *   3. On wake word → start STT streaming
 *   4. During STT → forward processed audio to hub
 *   5. On VAD end → stop STT streaming
 */
static void audio_pipeline_task(void *arg)
{
    size_t feed_chunk = speech_detect_feed_chunksize();
    size_t fetch_chunk = speech_detect_fetch_chunksize();

    /* Allocate buffers. */
    int16_t *mic_buf = malloc(feed_chunk * sizeof(int16_t));
    int16_t *out_buf = malloc(fetch_chunk * sizeof(int16_t));
    if (!mic_buf || !out_buf) {
        ESP_LOGE(TAG, "Failed to allocate audio buffers");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Audio pipeline started: feed=%zu fetch=%zu samples",
             feed_chunk, fetch_chunk);

    while (1) {
        /* Wait for HiveMind connection. */
        if (hm_client_get_state(s_client) != HM_STATE_READY) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        /* Mute check — skip all processing. */
        if (button_is_muted()) {
            if (s_streaming) {
                stt_session_stop();
            }
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        /* Read mic audio (blocking). */
        size_t samples_read = 0;
        esp_err_t ret = i2s_mic_read(mic_buf, feed_chunk, &samples_read);
        if (ret != ESP_OK || samples_read < feed_chunk) {
            continue;
        }

        /* Feed to ESP-SR AFE. */
        speech_detect_feed(mic_buf, feed_chunk);

        /* Fetch processed audio + detection events. */
        size_t out_samples = 0;
        speech_event_t evt = speech_detect_fetch(out_buf, &out_samples);

        switch (evt) {
            case SPEECH_EVT_WAKEWORD:
                ESP_LOGI(TAG, "Wake word detected!");
                s_state = SAT_WAKE_DETECTED;
                stt_session_start();
                break;

            case SPEECH_EVT_VAD_START:
                /* Speech ongoing — ensure we're streaming. */
                if (!s_streaming) {
                    stt_session_start();
                }
                break;

            case SPEECH_EVT_VAD_END:
                ESP_LOGI(TAG, "VAD: speech ended");
                stt_session_stop();
                break;

            case SPEECH_EVT_NONE:
                break;
        }

        /* Stream processed audio to hub during STT session. */
        if (s_streaming && out_samples > 0) {
            hm_send_binary(s_client, HM_BIN_STT_HANDLE,
                           (const uint8_t *)out_buf,
                           out_samples * sizeof(int16_t));
        }
    }
}

/* ── UI control task (button + mute + LED + TTS idle) ────────────── */

static void ui_task(void *arg)
{
    bool btn_listening = false;  /* Manual push-to-talk state. */

    ESP_LOGI(TAG, "UI task started");

    while (1) {
        /* Mute switch. */
        if (button_is_muted()) {
            if (s_state != SAT_MUTED) {
                s_state = SAT_MUTED;
                btn_listening = false;
                led_set_state(SAT_MUTED);
                ESP_LOGI(TAG, "Muted");
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        /* Center button: manual push-to-talk override.
         * This supplements the wake word — press to force-start/stop STT. */
        if (button_was_pressed()) {
            btn_listening = !btn_listening;
            if (btn_listening) {
                ESP_LOGI(TAG, "Manual listen (button)");
                stt_session_start();
            } else {
                ESP_LOGI(TAG, "Manual stop (button)");
                stt_session_stop();
            }
        }

        /* Clear manual state when session ends (e.g. VAD stopped it). */
        if (btn_listening && !s_streaming) {
            btn_listening = false;
        }

        /* Transition from SPEAKING → IDLE when playback finishes. */
        if (s_state == SAT_SPEAKING && i2s_spk_is_idle()) {
            s_state = SAT_IDLE;
        }

        led_set_state(s_state);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/* ── Entry point ─────────────────────────────────────────────────── */

void app_main(void)
{
    /* NVS (WiFi credentials storage). */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Network. */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(example_connect());

    /* Hardware init. */
    ESP_ERROR_CHECK(codec_init_all());
    ESP_ERROR_CHECK(i2s_mic_init());
    ESP_ERROR_CHECK(i2s_spk_init());
    ESP_ERROR_CHECK(led_ring_init());
    ESP_ERROR_CHECK(button_init());

    /* ESP-SR speech detection (WakeNet + VAD). */
    ESP_ERROR_CHECK(speech_detect_init());

    /* Start speaker playback task. */
    i2s_spk_start_task();

    /* Show idle state. */
    led_set_state(SAT_IDLE);

    /* HiveMind client. */
    hm_config_t config = {
        .host             = CONFIG_EXAMPLE_HIVEMIND_HOST,
        .port             = 5678,
        .username         = "voice-pe",
        .access_key       = CONFIG_EXAMPLE_HIVEMIND_KEY,
        .password         = CONFIG_EXAMPLE_HIVEMIND_PASSWORD,
        .site_id          = "voice-pe-satellite",
        .preferred_cipher = HM_CIPHER_AES_GCM,
        .reconnect_ms     = CONFIG_HIVEMIND_RECONNECT_MS,
    };

    ESP_ERROR_CHECK(hm_client_init(&s_client, &config));
    hm_client_set_bus_cb(s_client, on_bus_message);
    hm_client_set_binary_cb(s_client, on_binary);
    hm_client_set_state_cb(s_client, on_state_change);

    ESP_LOGI(TAG, "Connecting to HiveMind hub at %s:%d", config.host, config.port);
    ESP_ERROR_CHECK(hm_client_connect(s_client));

    /* Start audio pipeline (wake word + VAD + STT streaming). */
    xTaskCreate(audio_pipeline_task, "audio_pipe", 8192, NULL, 5, NULL);
    xTaskCreate(ui_task, "ui", 4096, NULL, 4, NULL);
}
