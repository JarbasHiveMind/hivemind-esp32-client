/**
 * @file i2s_mic.h
 * @brief I2S microphone input from XMOS Voice Kit codec.
 */
#ifndef I2S_MIC_H
#define I2S_MIC_H

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/**
 * @brief Initialize I2S RX for microphone input (XMOS codec).
 * @return ESP_OK on success.
 */
esp_err_t i2s_mic_init(void);

/**
 * @brief Read 16-bit mono PCM samples from the microphone.
 *
 * Reads 32-bit stereo I2S data from the XMOS codec, extracts the left channel,
 * and converts to 16-bit mono.
 *
 * @param buf           Output buffer for 16-bit signed PCM samples.
 * @param max_samples   Maximum number of samples to read.
 * @param samples_read  Actual number of samples read.
 * @return ESP_OK on success.
 */
esp_err_t i2s_mic_read(int16_t *buf, size_t max_samples, size_t *samples_read);

#endif /* I2S_MIC_H */
