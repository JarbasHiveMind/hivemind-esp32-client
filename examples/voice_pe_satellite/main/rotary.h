/**
 * @file rotary.h
 * @brief Rotary encoder driver for volume control (GPIO16/GPIO18).
 */
#ifndef ROTARY_H
#define ROTARY_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/**
 * @brief Initialize the rotary encoder on GPIO16 (A) / GPIO18 (B).
 * @return ESP_OK on success.
 */
esp_err_t rotary_init(void);

/**
 * @brief Get the current volume level (0–100).
 */
uint8_t rotary_get_volume(void);

/**
 * @brief Set the volume level (0–100). Clamps to range.
 */
void rotary_set_volume(uint8_t vol);

/**
 * @brief Check if volume changed since last call (clears flag).
 * @param out_volume  Current volume level (0–100).
 * @return true if volume changed.
 */
bool rotary_volume_changed(uint8_t *out_volume);

#endif /* ROTARY_H */
