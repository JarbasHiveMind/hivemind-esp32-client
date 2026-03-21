/**
 * @file vad_simple.c
 * @brief Simple energy-based VAD using RMS threshold.
 *
 * This is intentionally minimal — it only detects whether audio energy
 * exceeds a fixed threshold. For mic-satellite mode, this is sufficient
 * because the hub handles all speech processing.
 */
#include "vad_simple.h"
#include <math.h>

/* RMS energy threshold for speech detection.
 * ~200 for 16-bit audio works well for typical room noise.
 * Adjust based on mic sensitivity (XMOS with AGC outputs fairly hot signal). */
#define VAD_ENERGY_THRESHOLD 200

bool vad_is_speech(const int16_t *samples, size_t count)
{
    if (count == 0) {
        return false;
    }

    /* Compute RMS energy. */
    int64_t sum_sq = 0;
    for (size_t i = 0; i < count; i++) {
        int32_t s = samples[i];
        sum_sq += s * s;
    }
    double rms = sqrt((double)sum_sq / count);

    return rms > VAD_ENERGY_THRESHOLD;
}
