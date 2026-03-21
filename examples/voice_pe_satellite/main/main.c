/**
 * @file main.c
 * @brief HiveMind Voice PE satellite — push-to-talk audio satellite
 *        for the Home Assistant Voice Preview Edition hardware.
 *
 * Connects to a HiveMind hub via WebSocket. Streams microphone audio
 * when the center button is pressed, plays TTS responses through the
 * speaker, and indicates state via the 12-LED ring.
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

static const char *TAG = "voice_pe";

static hm_client_t *s_client = NULL;
static volatile sat_state_t s_state = SAT_IDLE;
static volatile bool s_listening = false;

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
}

static void on_binary(hm_client_t *client, hm_bin_type_t bin_type,
                       const uint8_t *data, size_t len)
{
    if (bin_type == HM_BIN_TTS_AUDIO) {
        ESP_LOGD(TAG, "TTS audio chunk: %zu bytes", len);
        s_state = SAT_SPEAKING;
        /* Push raw PCM (16-bit mono 16 kHz) into speaker ring buffer. */
        i2s_spk_push((const int16_t *)data, len / sizeof(int16_t));
    }
}

static void on_state_change(hm_client_t *client, hm_state_t state)
{
    ESP_LOGI(TAG, "HiveMind state: %d", state);

    if (state == HM_STATE_READY) {
        s_state = SAT_IDLE;
        ESP_LOGI(TAG, "Connected to hub — ready for push-to-talk");
    } else if (state == HM_STATE_DISCONNECTED) {
        s_state = SAT_ERROR;
    }
}

/* ── Audio capture task ──────────────────────────────────────────── */

static void audio_capture_task(void *arg)
{
    int16_t buf[256];
    size_t samples_read;

    ESP_LOGI(TAG, "Audio capture task started");

    while (1) {
        /* Wait for READY state. */
        if (hm_client_get_state(s_client) != HM_STATE_READY) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        /* Only stream when listening and not muted. */
        if (!s_listening || button_is_muted()) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        esp_err_t ret = i2s_mic_read(buf, 256, &samples_read);
        if (ret == ESP_OK && samples_read > 0) {
            hm_send_binary(s_client, HM_BIN_STT_HANDLE,
                           (const uint8_t *)buf,
                           samples_read * sizeof(int16_t));
        }
    }
}

/* ── UI control task (button + mute + LED) ───────────────────────── */

static void ui_task(void *arg)
{
    ESP_LOGI(TAG, "UI task started");

    while (1) {
        /* Check mute switch (overrides everything). */
        if (button_is_muted()) {
            if (s_state != SAT_MUTED) {
                s_state = SAT_MUTED;
                s_listening = false;
                led_set_state(SAT_MUTED);
                ESP_LOGI(TAG, "Muted");
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        /* Center button: toggle listening. */
        if (button_was_pressed()) {
            s_listening = !s_listening;
            if (s_listening) {
                s_state = SAT_LISTENING;
                ESP_LOGI(TAG, "Listening started (push-to-talk)");
                /* Notify hub that recording has begun. */
                hm_send_bus_message(s_client, "recognizer_loop:record_begin",
                                     NULL, NULL);
            } else {
                s_state = SAT_IDLE;
                ESP_LOGI(TAG, "Listening stopped");
                hm_send_bus_message(s_client, "recognizer_loop:record_end",
                                     NULL, NULL);
            }
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

    /* Hardware. */
    ESP_ERROR_CHECK(codec_init_all());
    ESP_ERROR_CHECK(i2s_mic_init());
    ESP_ERROR_CHECK(i2s_spk_init());
    ESP_ERROR_CHECK(led_ring_init());
    ESP_ERROR_CHECK(button_init());

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

    /* Start UI + audio tasks. */
    xTaskCreate(audio_capture_task, "audio_cap", 4096, NULL, 5, NULL);
    xTaskCreate(ui_task, "ui", 4096, NULL, 4, NULL);
}
