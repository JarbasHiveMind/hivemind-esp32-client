/**
 * @file led_ring.h
 * @brief WS2812 LED ring driver for satellite state indication.
 */
#ifndef LED_RING_H
#define LED_RING_H

#include "esp_err.h"
#include "voice_pe_hw.h"

/**
 * @brief Initialize the LED ring (RMT driver + power gate).
 * @return ESP_OK on success.
 */
esp_err_t led_ring_init(void);

/**
 * @brief Set all LEDs to the color corresponding to the satellite state.
 *
 * IDLE=dim white, LISTENING=blue, SPEAKING=green, ERROR=red, MUTED=orange.
 */
void led_set_state(sat_state_t state);

#endif /* LED_RING_H */
