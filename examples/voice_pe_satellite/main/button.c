/**
 * @file button.c
 * @brief Center button (GPIO0, active low) with ISR debounce + mute switch (GPIO3, polled).
 */
#include "button.h"
#include "voice_pe_hw.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

#include <stdatomic.h>

static const char *TAG = "button";

static atomic_bool s_btn_pressed = false;

/* Debounce: ignore edges within 200 ms of last press. */
static int64_t s_last_press_us = 0;
#define DEBOUNCE_US (200 * 1000)

static void IRAM_ATTR btn_isr_handler(void *arg)
{
    int64_t now = esp_timer_get_time();
    if ((now - s_last_press_us) > DEBOUNCE_US) {
        s_last_press_us = now;
        s_btn_pressed = true;
    }
}

esp_err_t button_init(void)
{
    /* Center button: GPIO0, active low, internal pullup. */
    gpio_config_t btn_cfg = {
        .pin_bit_mask = 1ULL << VP_BTN_PIN,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_NEGEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&btn_cfg));

    /* Mute switch: GPIO3, input (no interrupt — polled). */
    gpio_config_t mute_cfg = {
        .pin_bit_mask = 1ULL << VP_MUTE_PIN,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&mute_cfg));

    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(VP_BTN_PIN, btn_isr_handler, NULL));

    ESP_LOGI(TAG, "Button (GPIO%d) and mute switch (GPIO%d) initialized",
             VP_BTN_PIN, VP_MUTE_PIN);
    return ESP_OK;
}

bool button_was_pressed(void)
{
    bool expected = true;
    return atomic_compare_exchange_strong(&s_btn_pressed, &expected, false);
}

bool button_is_muted(void)
{
    /* Mute switch: high = muted (physical slider engaged). */
    return gpio_get_level(VP_MUTE_PIN) == 1;
}
