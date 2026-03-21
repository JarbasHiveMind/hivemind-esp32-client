/**
 * @file led_ring.c
 * @brief WS2812 LED ring via ESP-IDF led_strip component.
 *
 * 12 LEDs in GRB color order on GPIO21. Power gated via GPIO45.
 * Five solid-color states — no animations for MVP simplicity.
 */
#include "led_ring.h"
#include "voice_pe_hw.h"

#include "led_strip.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "led_ring";

static led_strip_handle_t s_strip = NULL;

esp_err_t led_ring_init(void)
{
    /* Power gate: enable LED supply. */
    gpio_config_t pwr_cfg = {
        .pin_bit_mask = 1ULL << VP_LED_POWER_PIN,
        .mode         = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&pwr_cfg));
    gpio_set_level(VP_LED_POWER_PIN, 1);

    /* Configure LED strip via RMT. */
    led_strip_config_t strip_cfg = {
        .strip_gpio_num   = VP_LED_PIN,
        .max_leds         = VP_LED_COUNT,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model        = LED_MODEL_WS2812,
    };
    led_strip_rmt_config_t rmt_cfg = {
        .resolution_hz = 10 * 1000 * 1000,  /* 10 MHz */
    };

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip));
    led_strip_clear(s_strip);

    ESP_LOGI(TAG, "LED ring initialized: %d LEDs on GPIO%d", VP_LED_COUNT, VP_LED_PIN);
    return ESP_OK;
}

void led_set_state(sat_state_t state)
{
    if (s_strip == NULL) {
        return;
    }

    uint8_t r = 0, g = 0, b = 0;

    switch (state) {
        case SAT_IDLE:          r = 16; g = 16; b = 16; break;  /* Dim white */
        case SAT_WAKE_DETECTED: r = 0;  g = 60; b = 60; break;  /* Cyan */
        case SAT_LISTENING:     r = 0;  g = 0;  b = 80; break;  /* Blue */
        case SAT_THINKING:      r = 60; g = 0;  b = 60; break;  /* Purple */
        case SAT_SPEAKING:      r = 0;  g = 80; b = 0;  break;  /* Green */
        case SAT_ERROR:         r = 80; g = 0;  b = 0;  break;  /* Red */
        case SAT_MUTED:         r = 80; g = 40; b = 0;  break;  /* Orange */
    }

    for (int i = 0; i < VP_LED_COUNT; i++) {
        led_strip_set_pixel(s_strip, i, r, g, b);
    }
    led_strip_refresh(s_strip);
}
