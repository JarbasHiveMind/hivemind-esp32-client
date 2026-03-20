/**
 * @file main.c
 * @brief HiveMind text satellite example — sends an utterance, logs responses.
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include "hivemind.h"

static const char *TAG = "hm_text";

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

static void on_state_change(hm_client_t *client, hm_state_t state)
{
    ESP_LOGI(TAG, "State: %d", state);

    if (state == HM_STATE_READY) {
        ESP_LOGI(TAG, "Connected — sending utterance");
        hm_send_utterance(client, "hello world");
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

    hm_config_t config = {
        .host         = CONFIG_EXAMPLE_HIVEMIND_HOST,
        .port         = 5678,
        .username     = "esp32-text",
        .access_key   = CONFIG_EXAMPLE_HIVEMIND_KEY,
        .password     = CONFIG_EXAMPLE_HIVEMIND_PASSWORD,
        .site_id      = "esp32-text-satellite",
        .preferred_cipher = HM_CIPHER_AES_GCM,
        .reconnect_ms = CONFIG_HIVEMIND_RECONNECT_MS,
    };

    hm_client_t *client = NULL;
    ESP_ERROR_CHECK(hm_client_init(&client, &config));
    hm_client_set_bus_cb(client, on_bus_message);
    hm_client_set_state_cb(client, on_state_change);

    ESP_LOGI(TAG, "Connecting to %s:%d", config.host, config.port);
    ESP_ERROR_CHECK(hm_client_connect(client));
}
