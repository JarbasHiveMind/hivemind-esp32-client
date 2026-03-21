/**
 * @file speech_detect.c
 * @brief ESP-SR AFE integration with WakeNet wake word + VAD.
 *
 * Uses Espressif's Audio Front-End (AFE) pipeline which provides:
 * - Noise Suppression (NS)
 * - Voice Activity Detection (VAD)
 * - Wake Word Detection (WakeNet9)
 *
 * Audio flow: I2S mic → feed() → AFE pipeline → fetch() → events + clean audio.
 *
 * The XMOS codec already provides AEC/NS/AGC, so AFE AEC is disabled.
 * Primary value from ESP-SR here is WakeNet + VAD.
 *
 * API targets ESP-SR v1.3.0. Key differences from v2.0+:
 * - v1.x uses esp_afe_handle_from_config() (v2 removed ESP_AFE_SR_HANDLE global)
 * - v1.x uses afe_config_init() with channel format string
 * - v1.x uses esp_srmodel_init/filter for model loading
 */
#include "speech_detect.h"

#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "esp_log.h"

#include <string.h>

static const char *TAG = "speech";

static esp_afe_sr_iface_t *s_afe = NULL;
static esp_afe_sr_data_t *s_afe_data = NULL;
static srmodel_list_t *s_models = NULL;
static int s_feed_chunksize = 0;
static int s_fetch_chunksize = 0;

/* Track whether we're in a speech session (between wake word and VAD end). */
static bool s_in_session = false;

/* Consecutive silence frames counter for VAD end detection. */
static int s_silence_frames = 0;
#define VAD_SILENCE_FRAMES_THRESHOLD 20  /* ~600ms at 30ms/frame */

esp_err_t speech_detect_init(void)
{
    /* Load speech recognition models from flash partition. */
    s_models = esp_srmodel_init("model");
    if (s_models == NULL) {
        ESP_LOGE(TAG, "Failed to load SR models from 'model' partition");
        return ESP_FAIL;
    }

    /* Get AFE config for single-mic input, speech recognition mode. */
    afe_config_t *cfg = afe_config_init("M", s_models, AFE_TYPE_SR, AFE_MODE_LOW_COST);
    if (cfg == NULL) {
        ESP_LOGE(TAG, "afe_config_init failed");
        return ESP_FAIL;
    }

    /* Select wake word model (first available WakeNet model). */
    cfg->wakenet_model_name = esp_srmodel_filter(s_models, ESP_WN_PREFIX, NULL);
    if (cfg->wakenet_model_name == NULL) {
        ESP_LOGW(TAG, "No WakeNet model found — wake word detection disabled");
    } else {
        ESP_LOGI(TAG, "Wake word model: %s", cfg->wakenet_model_name);
    }

    /* Disable AEC (XMOS handles it in hardware). */
    cfg->aec_init = false;

    /* Get AFE handle from config and create instance. */
    s_afe = esp_afe_handle_from_config(cfg);
    if (s_afe == NULL) {
        ESP_LOGE(TAG, "esp_afe_handle_from_config failed");
        afe_config_free(cfg);
        return ESP_FAIL;
    }

    s_afe_data = s_afe->create_from_config(cfg);
    if (s_afe_data == NULL) {
        ESP_LOGE(TAG, "AFE create_from_config failed");
        afe_config_free(cfg);
        return ESP_FAIL;
    }

    s_feed_chunksize = s_afe->get_feed_chunksize(s_afe_data);
    s_fetch_chunksize = s_afe->get_fetch_chunksize(s_afe_data);

    ESP_LOGI(TAG, "ESP-SR AFE initialized: feed=%d samples, fetch=%d samples",
             s_feed_chunksize, s_fetch_chunksize);

    afe_config_free(cfg);
    s_in_session = false;
    s_silence_frames = 0;
    return ESP_OK;
}

void speech_detect_feed(const int16_t *samples, size_t count)
{
    if (s_afe == NULL || s_afe_data == NULL) {
        return;
    }
    s_afe->feed(s_afe_data, (int16_t *)samples);
}

speech_event_t speech_detect_fetch(int16_t *out_audio, size_t *out_samples)
{
    if (s_afe == NULL || s_afe_data == NULL) {
        if (out_samples) *out_samples = 0;
        return SPEECH_EVT_NONE;
    }

    afe_fetch_result_t *res = s_afe->fetch(s_afe_data);
    if (res == NULL || res->ret_value == ESP_FAIL) {
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
        s_silence_frames = 0;
        return SPEECH_EVT_WAKEWORD;
    }

    /* VAD state transitions (only meaningful during a session). */
    if (s_in_session) {
        if (res->vad_state == VAD_SPEECH) {
            s_silence_frames = 0;
            return SPEECH_EVT_VAD_START;
        }
        /* Count consecutive silence frames before declaring speech end.
         * This prevents premature cutoff on short pauses between words. */
        s_silence_frames++;
        if (s_silence_frames >= VAD_SILENCE_FRAMES_THRESHOLD) {
            s_in_session = false;
            s_silence_frames = 0;
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
    if (s_afe && s_afe_data) {
        s_afe->destroy(s_afe_data);
        s_afe_data = NULL;
    }
    s_afe = NULL;
    s_in_session = false;
    s_silence_frames = 0;
    /* Note: s_models not freed — lives for program lifetime. */
}
