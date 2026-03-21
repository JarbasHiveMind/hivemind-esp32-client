/**
 * @file audio_util.c
 * @brief Audio format detection, WAV parsing, and volume scaling.
 */
#include "audio_util.h"
#include <string.h>

bool audio_is_wav(const uint8_t *data, size_t len)
{
    if (len < 12) return false;
    return memcmp(data, "RIFF", 4) == 0 && memcmp(data + 8, "WAVE", 4) == 0;
}

bool audio_wav_extract_pcm(const uint8_t *wav_data, size_t wav_len,
                            const int16_t **out_pcm, size_t *out_samples,
                            uint32_t *out_sample_rate, uint16_t *out_channels)
{
    if (!audio_is_wav(wav_data, wav_len)) {
        return false;
    }

    /* Parse fmt chunk for sample rate and channels. */
    if (wav_len >= 28) {
        if (out_sample_rate) {
            memcpy(out_sample_rate, wav_data + 24, 4);
        }
        if (out_channels) {
            memcpy(out_channels, wav_data + 22, 2);
        }
    }

    /* Walk chunks to find "data". */
    size_t pos = 12;
    while (pos + 8 <= wav_len) {
        uint32_t chunk_size;
        memcpy(&chunk_size, wav_data + pos + 4, 4);

        if (memcmp(wav_data + pos, "data", 4) == 0) {
            size_t data_offset = pos + 8;
            size_t data_bytes = chunk_size;
            if (data_offset + data_bytes > wav_len) {
                data_bytes = wav_len - data_offset;
            }
            *out_pcm = (const int16_t *)(wav_data + data_offset);
            *out_samples = data_bytes / sizeof(int16_t);
            return true;
        }

        pos += 8 + chunk_size;
        if (pos % 2 != 0) pos++;  /* WAV chunks are word-aligned. */
    }

    return false;
}

void audio_apply_volume(int16_t *pcm, size_t samples, uint8_t volume)
{
    if (volume >= 100) return;  /* No scaling needed. */
    if (volume == 0) {
        memset(pcm, 0, samples * sizeof(int16_t));
        return;
    }

    /* Fixed-point scaling: vol/100 ≈ vol*256/25600 with 8-bit fraction. */
    uint32_t scale = ((uint32_t)volume * 256) / 100;
    for (size_t i = 0; i < samples; i++) {
        int32_t s = ((int32_t)pcm[i] * (int32_t)scale) >> 8;
        pcm[i] = (int16_t)s;
    }
}
