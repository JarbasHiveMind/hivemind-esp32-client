/**
 * @file main.c
 * @brief HiveMind mic satellite example — streams I2S audio, handles TTS responses.
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include "driver/i2s.h"
#include "hivemind.h"

static const char *TAG = "hm_mic";

#define I2S_SAMPLE_RATE     16000
#define I2S_SAMPLE_BITS     I2S_BITS_PER_SAMPLE_16BIT
#define I2S_CHANNEL         I2S_CHANNEL_MONO
#define I2S_PORT            I2S_NUM_0
#define AUDIO_BUF_SIZE      1024

/* INMP441 pin assignments — adjust per board */
#define I2S_BCK_PIN         26
#define I2S_WS_PIN          25
#define I2S_DATA_IN_PIN     22

static hm_client_t *s_client = NULL;

static void i2s_init(void)
{
    i2s_config_t cfg = {
        .mode                 = I2S_MODE_MASTER | I2S_MODE_RX,
        .sample_rate          = I2S_SAMPLE_RATE,
        .bits_per_sample      = I2S_SAMPLE_BITS,
        .channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count        = 4,
        .dma_buf_len          = AUDIO_BUF_SIZE,
        .use_apll             = false,
    };

    i2s_pin_config_t pins = {
        .bck_io_num   = I2S_BCK_PIN,
        .ws_io_num    = I2S_WS_PIN,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num  = I2S_DATA_IN_PIN,
    };

    ESP_ERROR_CHECK(i2s_driver_install(I2S_PORT, &cfg, 0, NULL));
    ESP_ERROR_CHECK(i2s_set_pin(I2S_PORT, &pins));
}

static void audio_capture_task(void *arg)
{
    uint8_t buf[AUDIO_BUF_SIZE];
    size_t bytes_read = 0;

    ESP_LOGI(TAG, "Audio capture started");

    while (1) {
        if (hm_client_get_state(s_client) != HM_STATE_READY) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        esp_err_t ret = i2s_read(I2S_PORT, buf, sizeof(buf), &bytes_read, portMAX_DELAY);
        if (ret == ESP_OK && bytes_read > 0) {
            hm_send_binary(s_client, HM_BIN_STT_HANDLE, buf, bytes_read);
        }
    }
}

static void on_bus_message(hm_client_t *client, const char *type,
                           cJSON *data, cJSON *context)
{
    ESP_LOGI(TAG, "Bus message: type=%s", type);

    if (strcmp(type, "speak") == 0) {
        cJSON *utterance = cJSON_GetObjectItem(data, "utterance");
        if (utterance && cJSON_IsString(utterance)) {
            ESP_LOGI(TAG, "TTS: %s", utterance->valuestring);
        }
    }
}

static void on_binary(hm_client_t *client, hm_bin_type_t bin_type,
                      const uint8_t *data, size_t len)
{
    if (bin_type == HM_BIN_TTS_AUDIO) {
        ESP_LOGI(TAG, "Received TTS audio: %zu bytes", len);
        /* TODO: feed to I2S output / DAC */
    }
}

static void on_state_change(hm_client_t *client, hm_state_t state)
{
    ESP_LOGI(TAG, "State: %d", state);

    if (state == HM_STATE_READY) {
        ESP_LOGI(TAG, "Connected — starting audio capture");
        xTaskCreate(audio_capture_task, "audio_cap", 4096, NULL, 5, NULL);
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(example_connect());

    i2s_init();

    hm_config_t config = {
        .host         = CONFIG_EXAMPLE_HIVEMIND_HOST,
        .port         = 5678,
        .username     = "esp32-mic",
        .access_key   = CONFIG_EXAMPLE_HIVEMIND_KEY,
        .password     = CONFIG_EXAMPLE_HIVEMIND_PASSWORD,
        .site_id      = "esp32-mic-satellite",
        .preferred_cipher = HM_CIPHER_AES_GCM,
        .reconnect_ms = CONFIG_HIVEMIND_RECONNECT_MS,
    };

    ESP_ERROR_CHECK(hm_client_init(&s_client, &config));
    hm_client_set_bus_cb(s_client, on_bus_message);
    hm_client_set_binary_cb(s_client, on_binary);
    hm_client_set_state_cb(s_client, on_state_change);

    ESP_LOGI(TAG, "Connecting to %s:%d", config.host, config.port);
    ESP_ERROR_CHECK(hm_client_connect(s_client));
}
