/**
 * @file i2s_spk.h
 * @brief I2S speaker output to TI AIC3204 DAC with TTS playback ring buffer.
 */
#ifndef I2S_SPK_H
#define I2S_SPK_H

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/**
 * @brief Initialize I2S TX for speaker output and enable the amplifier.
 * @return ESP_OK on success.
 */
esp_err_t i2s_spk_init(void);

/**
 * @brief Push TTS audio into the playback ring buffer.
 *
 * Accepts 16-bit mono PCM at 16 kHz. The playback task handles
 * upsampling to 48 kHz stereo 32-bit for the AIC3204.
 *
 * @param pcm16k_mono  16-bit signed PCM samples at 16 kHz.
 * @param samples      Number of samples.
 * @return ESP_OK on success, ESP_ERR_TIMEOUT if ring buffer is full.
 */
esp_err_t i2s_spk_push(const int16_t *pcm16k_mono, size_t samples);

/**
 * @brief Start the playback FreeRTOS task.
 *
 * The task drains the ring buffer and writes to I2S. Call after i2s_spk_init().
 */
void i2s_spk_start_task(void);

/**
 * @brief Check whether the playback ring buffer is empty.
 * @return true if no pending audio data.
 */
bool i2s_spk_is_idle(void);

#endif /* I2S_SPK_H */
