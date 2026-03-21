/**
 * @file speech_detect.h
 * @brief ESP-SR integration: Audio Front-End with WakeNet and VAD.
 *
 * Provides wake word detection ("hi_esp") and voice activity detection
 * using Espressif's ESP-SR framework. Audio is fed from the I2S mic
 * and processed through the AFE pipeline.
 */
#ifndef SPEECH_DETECT_H
#define SPEECH_DETECT_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

/** Speech detection events reported to the main application. */
typedef enum {
    SPEECH_EVT_NONE,          /**< No event (still processing). */
    SPEECH_EVT_WAKEWORD,      /**< Wake word detected — begin STT session. */
    SPEECH_EVT_VAD_START,     /**< Voice activity started (speech begins). */
    SPEECH_EVT_VAD_END,       /**< Voice activity ended (silence after speech). */
} speech_event_t;

/**
 * @brief Initialize ESP-SR AFE with WakeNet and VAD.
 *
 * Configures the audio front-end for single-channel 16 kHz 16-bit input.
 * Loads the "hi_esp" wake word model.
 *
 * @return ESP_OK on success.
 */
esp_err_t speech_detect_init(void);

/**
 * @brief Feed a chunk of 16-bit mono PCM audio into the AFE pipeline.
 *
 * Call this with 30 ms frames (480 samples at 16 kHz).
 * The AFE processes the audio internally.
 *
 * @param samples  16-bit signed PCM samples at 16 kHz.
 * @param count    Number of samples (should be feed_chunksize from init).
 */
void speech_detect_feed(const int16_t *samples, size_t count);

/**
 * @brief Fetch the latest detection result from the AFE.
 *
 * Returns processed audio (after AEC/NS) and the detection event.
 * The processed audio can be forwarded to HiveMind for STT.
 *
 * @param out_audio     Output buffer for processed audio (may be NULL).
 * @param out_samples   Number of processed samples written.
 * @return Speech detection event.
 */
speech_event_t speech_detect_fetch(int16_t *out_audio, size_t *out_samples);

/**
 * @brief Get the required feed chunk size in samples.
 *
 * The caller must provide exactly this many samples per feed() call.
 * Typically 480 samples (30 ms at 16 kHz).
 */
size_t speech_detect_feed_chunksize(void);

/**
 * @brief Get the fetch output chunk size in samples.
 */
size_t speech_detect_fetch_chunksize(void);

/**
 * @brief Destroy the AFE and free resources.
 */
void speech_detect_deinit(void);

#endif /* SPEECH_DETECT_H */
