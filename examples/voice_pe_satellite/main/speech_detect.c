/**
 * @file speech_detect.c
 * @brief ESP-SR AFE integration with WakeNet wake word + VAD.
 *
 * Uses Espressif's Audio Front-End (AFE) pipeline which provides:
 * - Noise Suppression (NS)
 * - Voice Activity Detection (VAD)
 * - Wake Word Detection (WakeNet9, "hi_esp" model)
 *
 * Audio flow: I2S mic → feed() → AFE pipeline → fetch() → events + clean audio.
 *
 * The XMOS codec already provides AEC/NS/AGC, so the AFE NS is supplementary.
 * Primary value from ESP-SR here is WakeNet + VAD.
 */
#include "speech_detect.h"

#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_mn_iface.h"
#include "esp_log.h"

#include <string.h>

static const char *TAG = "speech";

static const esp_afe_sr_iface_t *s_afe = NULL;
static afe_handle_t s_afe_handle = NULL;
static int s_feed_chunksize = 0;
static int s_fetch_chunksize = 0;

/* Track whether we're in a speech session (between wake word and VAD end). */
static bool s_in_session = false;

esp_err_t speech_detect_init(void)
{
    s_afe = &ESP_AFE_SR_HANDLE;

    afe_config_t cfg = AFE_CONFIG_DEFAULT();

    /* Single microphone, no echo reference (XMOS handles AEC). */
    cfg.pcm_config.total_ch_num = 1;
    cfg.pcm_config.mic_num = 1;
    cfg.pcm_config.ref_num = 0;
    cfg.pcm_config.sample_rate = 16000;

    /* Enable wake word detection. */
    cfg.wakenet_init = true;
    cfg.wakenet_model_name = "wn9_hiesp";

    /* Enable VAD for speech endpoint detection. */
    cfg.vad_init = true;
    cfg.vad_min_speech_ms = 128;
    cfg.vad_min_noise_ms = 500;

    /* Disable AEC (XMOS handles it). Keep NS for additional filtering. */
    cfg.aec_init = false;

    s_afe_handle = s_afe->create_from_config(&cfg);
    if (s_afe_handle == NULL) {
        ESP_LOGE(TAG, "AFE creation failed");
        return ESP_FAIL;
    }

    s_feed_chunksize = s_afe->get_feed_chunksize(s_afe_handle);
    s_fetch_chunksize = s_afe->get_fetch_chunksize(s_afe_handle);

    ESP_LOGI(TAG, "ESP-SR AFE initialized: feed=%d samples, fetch=%d samples",
             s_feed_chunksize, s_fetch_chunksize);
    ESP_LOGI(TAG, "Wake word: \"hi_esp\" (WakeNet9)");

    s_in_session = false;
    return ESP_OK;
}

void speech_detect_feed(const int16_t *samples, size_t count)
{
    if (s_afe == NULL || s_afe_handle == NULL) {
        return;
    }
    /* AFE feed expects int16_t* (non-const in API). */
    s_afe->feed(s_afe_handle, (int16_t *)samples);
}

speech_event_t speech_detect_fetch(int16_t *out_audio, size_t *out_samples)
{
    if (s_afe == NULL || s_afe_handle == NULL) {
        if (out_samples) *out_samples = 0;
        return SPEECH_EVT_NONE;
    }

    afe_fetch_result_t *res = s_afe->fetch(s_afe_handle);
    if (res == NULL) {
        if (out_samples) *out_samples = 0;
        return SPEECH_EVT_NONE;
    }

    /* Copy processed audio for STT streaming. */
    if (out_audio && out_samples) {
        *out_samples = s_fetch_chunksize;
        memcpy(out_audio, res->data, s_fetch_chunksize * sizeof(int16_t));
    }

    /* Wake word detection. */
    if (res->wakeup_state == WAKENET_DETECTED) {
        ESP_LOGI(TAG, "Wake word detected!");
        s_in_session = true;
        return SPEECH_EVT_WAKEWORD;
    }

    /* VAD state transitions (only meaningful during a session). */
    if (s_in_session) {
        if (res->vad_state == AFE_VAD_SPEECH) {
            return SPEECH_EVT_VAD_START;
        }
        if (res->vad_state == AFE_VAD_NOISE || res->vad_state == AFE_VAD_SILENCE) {
            /* Speech ended — close the session. */
            s_in_session = false;
            return SPEECH_EVT_VAD_END;
        }
    }

    return SPEECH_EVT_NONE;
}

size_t speech_detect_feed_chunksize(void)
{
    return (size_t)s_feed_chunksize;
}

size_t speech_detect_fetch_chunksize(void)
{
    return (size_t)s_fetch_chunksize;
}

void speech_detect_deinit(void)
{
    if (s_afe && s_afe_handle) {
        s_afe->destroy(s_afe_handle);
        s_afe_handle = NULL;
    }
    s_in_session = false;
}
