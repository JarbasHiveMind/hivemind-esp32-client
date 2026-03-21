/**
 * @file button.h
 * @brief Center button (push-to-talk) and hardware mute switch.
 */
#ifndef BUTTON_H
#define BUTTON_H

#include <stdbool.h>
#include "esp_err.h"

/**
 * @brief Initialize button GPIO (ISR) and mute switch (polled).
 * @return ESP_OK on success.
 */
esp_err_t button_init(void);

/**
 * @brief Check if the center button was pressed (clears the flag).
 * @return true if pressed since last call.
 */
bool button_was_pressed(void);

/**
 * @brief Check the hardware mute switch state.
 * @return true if the mute switch is active (mic disabled).
 */
bool button_is_muted(void);

#endif /* BUTTON_H */
