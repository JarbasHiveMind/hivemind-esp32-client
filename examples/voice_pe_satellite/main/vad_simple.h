/**
 * @file vad_simple.h
 * @brief Simple energy-based Voice Activity Detection.
 *
 * Used in MODE_MIC_SATELLITE where ESP-SR is not needed.
 * Computes RMS energy of a frame and compares against a threshold.
 */
#ifndef VAD_SIMPLE_H
#define VAD_SIMPLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Check if an audio frame contains speech (energy above threshold).
 *
 * @param samples  16-bit signed PCM samples.
 * @param count    Number of samples in the frame.
 * @return true if speech detected, false if silence.
 */
bool vad_is_speech(const int16_t *samples, size_t count);

#endif /* VAD_SIMPLE_H */
