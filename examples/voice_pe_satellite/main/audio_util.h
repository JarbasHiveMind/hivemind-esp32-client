/**
 * @file audio_util.h
 * @brief Audio format detection and conversion utilities.
 *
 * TTS audio from the hub may arrive in different formats:
 * - Raw 16-bit PCM (no header)
 * - WAV-wrapped PCM (44-byte header + PCM data)
 * - Base64-encoded WAV (handled separately in main.c)
 *
 * This module detects the format and extracts raw PCM.
 */
#ifndef AUDIO_UTIL_H
#define AUDIO_UTIL_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Check if a buffer starts with a WAV/RIFF header.
 *
 * @param data  Buffer to check.
 * @param len   Buffer length.
 * @return true if it starts with "RIFF....WAVE".
 */
bool audio_is_wav(const uint8_t *data, size_t len);

/**
 * @brief Extract raw PCM data from a WAV buffer.
 *
 * Finds the "data" chunk and returns a pointer into the buffer
 * (zero-copy) along with the number of PCM samples.
 *
 * @param wav_data      WAV buffer.
 * @param wav_len       WAV buffer length.
 * @param out_pcm       Output: pointer to PCM data within wav_data.
 * @param out_samples   Output: number of 16-bit samples.
 * @param out_sample_rate  Output: sample rate from WAV header (may be NULL).
 * @param out_channels     Output: channel count from WAV header (may be NULL).
 * @return true on success, false if not valid WAV.
 */
bool audio_wav_extract_pcm(const uint8_t *wav_data, size_t wav_len,
                            const int16_t **out_pcm, size_t *out_samples,
                            uint32_t *out_sample_rate, uint16_t *out_channels);

/**
 * @brief Apply volume scaling to PCM audio in-place.
 *
 * @param pcm      16-bit signed PCM buffer (modified in-place).
 * @param samples  Number of samples.
 * @param volume   Volume level 0–100 (100 = full volume).
 */
void audio_apply_volume(int16_t *pcm, size_t samples, uint8_t volume);

#endif /* AUDIO_UTIL_H */
