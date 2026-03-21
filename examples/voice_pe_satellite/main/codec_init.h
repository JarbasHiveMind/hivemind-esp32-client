/**
 * @file codec_init.h
 * @brief I2C bus, XMOS Voice Kit, and TI AIC3204 codec initialization.
 */
#ifndef CODEC_INIT_H
#define CODEC_INIT_H

#include "esp_err.h"

/**
 * @brief Initialize all codecs: I2C bus, XMOS reset + probe, AIC3204 DAC config.
 * @return ESP_OK on success.
 */
esp_err_t codec_init_all(void);

#endif /* CODEC_INIT_H */
