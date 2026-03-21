/**
 * @file rotary.c
 * @brief Rotary encoder driver using GPIO ISR for quadrature decoding.
 *
 * Uses the Voice PE rotary dial (GPIO16=A, GPIO18=B) for volume control.
 * Quadrature decoding: both pins have ISRs on any edge. Direction is
 * determined by reading the other pin's level at the time of the edge.
 *
 * Volume range: 0–100, step size 5 per detent.
 */
#include "rotary.h"
#include "voice_pe_hw.h"

#include "driver/gpio.h"
#include "esp_log.h"

#include <stdatomic.h>
#include <stdbool.h>

static const char *TAG = "rotary";

#define VOL_STEP     5
#define VOL_DEFAULT  70
#define VOL_MIN      0
#define VOL_MAX      100

static atomic_int s_volume = VOL_DEFAULT;
static atomic_bool s_changed = false;

static void IRAM_ATTR rotary_isr_a(void *arg)
{
    int a = gpio_get_level(VP_DIAL_A);
    int b = gpio_get_level(VP_DIAL_B);

    /* Clockwise: A leads B → A rising while B low, or A falling while B high. */
    if (a != b) {
        /* Clockwise → volume up. */
        int v = atomic_load(&s_volume) + VOL_STEP;
        if (v > VOL_MAX) v = VOL_MAX;
        atomic_store(&s_volume, v);
    } else {
        /* Counter-clockwise → volume down. */
        int v = atomic_load(&s_volume) - VOL_STEP;
        if (v < VOL_MIN) v = VOL_MIN;
        atomic_store(&s_volume, v);
    }
    atomic_store(&s_changed, true);
}

esp_err_t rotary_init(void)
{
    gpio_config_t enc_cfg = {
        .pin_bit_mask = (1ULL << VP_DIAL_A) | (1ULL << VP_DIAL_B),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_ANYEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&enc_cfg));

    /* Only ISR on channel A; read B to determine direction. */
    gpio_set_intr_type(VP_DIAL_B, GPIO_INTR_DISABLE);
    ESP_ERROR_CHECK(gpio_isr_handler_add(VP_DIAL_A, rotary_isr_a, NULL));

    ESP_LOGI(TAG, "Rotary encoder initialized: A=GPIO%d B=GPIO%d, volume=%d",
             VP_DIAL_A, VP_DIAL_B, VOL_DEFAULT);
    return ESP_OK;
}

uint8_t rotary_get_volume(void)
{
    return (uint8_t)atomic_load(&s_volume);
}

void rotary_set_volume(uint8_t vol)
{
    if (vol > VOL_MAX) vol = VOL_MAX;
    atomic_store(&s_volume, (int)vol);
    atomic_store(&s_changed, true);
}

bool rotary_volume_changed(uint8_t *out_volume)
{
    bool expected = true;
    bool was_changed = atomic_compare_exchange_strong(&s_changed, &expected, false);
    if (out_volume) {
        *out_volume = (uint8_t)atomic_load(&s_volume);
    }
    return was_changed;
}
