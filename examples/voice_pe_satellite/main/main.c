/**
 * @file main.c
 * @brief HiveMind Voice PE satellite — configurable audio satellite
 *        for the Home Assistant Voice Preview Edition hardware.
 *
 * Three independent configuration axes (set via menuconfig):
 *
 *   Listening mode (VAD / Wake Word):
 *     - LISTEN_VAD_ONLY:   Continuous VAD, raw audio stream. WW on hub.
 *     - LISTEN_WAKE_WORD:  ESP-SR WakeNet + VAD. Records after wake word.
 *
 *   STT transport:
 *     - STT_HM_BINARY: Stream PCM chunks to hub (HM_BIN_RAW_AUDIO/STT_HANDLE)
 *     - STT_HM_B64:    Batch WAV as base64 via recognizer_loop:b64_transcribe
 *     - STT_HTTP:       POST WAV to OVOS STT HTTP server
 *
 *   TTS transport:
 *     - TTS_HM_BINARY: Hub pushes HM_BIN_TTS_AUDIO binary chunks
 *     - TTS_HM_B64:    Request/response via speak:b64_audio bus messages
 *     - TTS_HTTP:       GET from OVOS TTS HTTP server
 */
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include "mbedtls/base64.h"

#include "hivemind.h"
#include "voice_pe_hw.h"
#include "codec_init.h"
#include "i2s_mic.h"
#include "i2s_spk.h"
#include "led_ring.h"
#include "button.h"
#include "rotary.h"
#include "speech_detect.h"
#include "vad_simple.h"
#include "ovos_http.h"
#include "audio_util.h"

static const char *TAG = "voice_pe";

static hm_client_t *s_client = NULL;
static volatile sat_state_t s_state = SAT_IDLE;
static volatile bool s_streaming = false;

/* Language from menuconfig (default "en-us"). */
#ifdef CONFIG_EXAMPLE_LANGUAGE
static const char *s_lang = CONFIG_EXAMPLE_LANGUAGE;
#else
static const char *s_lang = "en-us";
#endif

/* ── Build-time config ───────────────────────────────────────────── */

#ifdef CONFIG_EXAMPLE_LISTEN_VAD_ONLY
static const listen_mode_t s_listen = LISTEN_VAD_ONLY;
#else
static const listen_mode_t s_listen = LISTEN_WAKE_WORD;
#endif

#if defined(CONFIG_EXAMPLE_STT_HM_B64)
static const stt_mode_t s_stt = STT_HM_B64;
#elif defined(CONFIG_EXAMPLE_STT_HTTP)
static const stt_mode_t s_stt = STT_HTTP;
#else
static const stt_mode_t s_stt = STT_HM_BINARY;
#endif

#if defined(CONFIG_EXAMPLE_TTS_HM_B64)
static const tts_mode_t s_tts = TTS_HM_B64;
#elif defined(CONFIG_EXAMPLE_TTS_HTTP)
static const tts_mode_t s_tts = TTS_HTTP;
#else
static const tts_mode_t s_tts = TTS_HM_BINARY;
#endif

/* ── Recording buffer (for STT_HM_B64 and STT_HTTP) ─────────────── */

#define REC_MAX_SAMPLES (16000 * 15)  /* Up to 15s of audio */
static int16_t *s_rec_buf = NULL;
static size_t s_rec_count = 0;

/* ── Base64 WAV encoding ─────────────────────────────────────────── */

static void build_wav_header(uint8_t *hdr, uint32_t data_bytes)
{
    uint32_t file_size = data_bytes + 36;
    uint32_t sr = 16000;
    uint16_t ch = 1, bits = 16;
    uint32_t byte_rate = sr * ch * bits / 8;
    uint16_t block_align = ch * bits / 8;
    uint32_t fmt_size = 16;
    uint16_t fmt_pcm = 1;

    memcpy(hdr, "RIFF", 4);      memcpy(hdr + 4, &file_size, 4);
    memcpy(hdr + 8, "WAVEfmt ", 8);
    memcpy(hdr + 16, &fmt_size, 4); memcpy(hdr + 20, &fmt_pcm, 2);
    memcpy(hdr + 22, &ch, 2);    memcpy(hdr + 24, &sr, 4);
    memcpy(hdr + 28, &byte_rate, 4); memcpy(hdr + 32, &block_align, 2);
    memcpy(hdr + 34, &bits, 2);  memcpy(hdr + 36, "data", 4);
    memcpy(hdr + 40, &data_bytes, 4);
}

static char *base64_encode_wav(const int16_t *pcm, size_t num_samples)
{
    size_t data_bytes = num_samples * sizeof(int16_t);
    size_t wav_size = 44 + data_bytes;
    uint8_t *wav = malloc(wav_size);
    if (!wav) return NULL;

    build_wav_header(wav, data_bytes);
    memcpy(wav + 44, pcm, data_bytes);

    size_t b64_len = 0;
    mbedtls_base64_encode(NULL, 0, &b64_len, wav, wav_size);
    char *b64 = malloc(b64_len + 1);
    if (b64) {
        mbedtls_base64_encode((unsigned char *)b64, b64_len + 1, &b64_len,
                               wav, wav_size);
        b64[b64_len] = '\0';
    }
    free(wav);
    return b64;
}

static void decode_b64_wav_to_speaker(const char *b64_str)
{
    size_t b64_len = strlen(b64_str);
    size_t wav_len = 0;
    mbedtls_base64_decode(NULL, 0, &wav_len,
                           (const unsigned char *)b64_str, b64_len);
    uint8_t *wav = malloc(wav_len);
    if (!wav) return;

    mbedtls_base64_decode(wav, wav_len, &wav_len,
                           (const unsigned char *)b64_str, b64_len);

    const int16_t *pcm;
    size_t samples;
    if (audio_wav_extract_pcm(wav, wav_len, &pcm, &samples, NULL, NULL)) {
        s_state = SAT_SPEAKING;
        int16_t *buf = malloc(samples * sizeof(int16_t));
        if (buf) {
            memcpy(buf, pcm, samples * sizeof(int16_t));
            audio_apply_volume(buf, samples, rotary_get_volume());
            i2s_spk_push(buf, samples);
            free(buf);
        }
    }
    free(wav);
}

/* ── STT submission ──────────────────────────────────────────────── */

static void submit_stt_recording(void)
{
    if (s_rec_count == 0) return;

    ESP_LOGI(TAG, "Submitting %zu samples for STT (mode=%d)", s_rec_count, s_stt);
    s_state = SAT_THINKING;

    if (s_stt == STT_HM_B64) {
        char *b64 = base64_encode_wav(s_rec_buf, s_rec_count);
        if (b64) {
            cJSON *data = cJSON_CreateObject();
            cJSON_AddStringToObject(data, "audio", b64);
            cJSON_AddStringToObject(data, "lang", s_lang);
            hm_send_bus_message(s_client, "recognizer_loop:b64_transcribe",
                                 data, NULL);
            cJSON_Delete(data);
            free(b64);
        }
    } else if (s_stt == STT_HTTP) {
        char transcript[512] = "";
        if (ovos_http_stt(s_rec_buf, s_rec_count, s_lang,
                           transcript, sizeof(transcript)) == ESP_OK &&
            transcript[0] != '\0') {
            cJSON *utt_data = cJSON_CreateObject();
            cJSON *arr = cJSON_CreateArray();
            cJSON_AddItemToArray(arr, cJSON_CreateString(transcript));
            cJSON_AddItemToObject(utt_data, "utterances", arr);
            cJSON_AddStringToObject(utt_data, "lang", s_lang);
            hm_send_bus_message(s_client, "recognizer_loop:utterance",
                                 utt_data, NULL);
            cJSON_Delete(utt_data);
        } else {
            s_state = SAT_IDLE;
        }
    }
    /* STT_HM_BINARY: audio was already streamed chunk-by-chunk. */
}

/* ── TTS request ─────────────────────────────────────────────────── */

static void request_tts(const char *utterance)
{
    ESP_LOGI(TAG, "TTS request (mode=%d): %s", s_tts, utterance);

    if (s_tts == TTS_HM_B64) {
        cJSON *req = cJSON_CreateObject();
        cJSON_AddStringToObject(req, "utterance", utterance);
        cJSON_AddBoolToObject(req, "listen", false);
        hm_send_bus_message(s_client, "speak:b64_audio", req, NULL);
        cJSON_Delete(req);
    } else if (s_tts == TTS_HTTP) {
        s_state = SAT_SPEAKING;
        int16_t *tts_buf = malloc(16000 * 30 * sizeof(int16_t));
        if (tts_buf) {
            size_t tts_samples = 0;
            if (ovos_http_tts(utterance, s_lang, tts_buf, 16000 * 30,
                               &tts_samples) == ESP_OK && tts_samples > 0) {
                audio_apply_volume(tts_buf, tts_samples, rotary_get_volume());
                i2s_spk_push(tts_buf, tts_samples);
            } else {
                s_state = SAT_IDLE;
            }
            free(tts_buf);
        }
    }
    /* TTS_HM_BINARY: hub pushes audio via on_binary callback. No request needed
     * beyond the hub's own "speak" → TTS pipeline. Hub sends HM_BIN_TTS_AUDIO. */
}

/* ── HiveMind callbacks ──────────────────────────────────────────── */

static void on_bus_message(hm_client_t *client, const char *type,
                            cJSON *data, cJSON *context)
{
    ESP_LOGI(TAG, "Bus: %s", type);

    /* speak → request TTS (for b64/http modes) or just log (binary mode). */
    if (strcmp(type, "speak") == 0) {
        cJSON *utt = cJSON_GetObjectItem(data, "utterance");
        if (utt && cJSON_IsString(utt)) {
            ESP_LOGI(TAG, "TTS: %s", utt->valuestring);
            if (s_tts != TTS_HM_BINARY) {
                request_tts(utt->valuestring);
            }
            /* TTS_HM_BINARY: hub will send audio via on_binary. */
        }
    }

    /* Base64 TTS response. */
    if (strcmp(type, "speak:b64_audio.response") == 0) {
        cJSON *audio = cJSON_GetObjectItem(data, "audio");
        if (audio && cJSON_IsString(audio)) {
            decode_b64_wav_to_speaker(audio->valuestring);
        }
    }

    /* Base64 STT transcription response. */
    if (strcmp(type, "recognizer_loop:b64_transcribe.response") == 0) {
        cJSON *transcripts = cJSON_GetObjectItem(data, "transcriptions");
        if (transcripts && cJSON_IsArray(transcripts) &&
            cJSON_GetArraySize(transcripts) > 0) {
            cJSON *first = cJSON_GetArrayItem(transcripts, 0);
            if (cJSON_IsArray(first) && cJSON_GetArraySize(first) > 0) {
                cJSON *text = cJSON_GetArrayItem(first, 0);
                if (cJSON_IsString(text)) {
                    ESP_LOGI(TAG, "Transcript: %s", text->valuestring);
                    cJSON *utt_data = cJSON_CreateObject();
                    cJSON *arr = cJSON_CreateArray();
                    cJSON_AddItemToArray(arr, cJSON_CreateString(text->valuestring));
                    cJSON_AddItemToObject(utt_data, "utterances", arr);
                    cJSON_AddStringToObject(utt_data, "lang", s_lang);
                    hm_send_bus_message(client, "recognizer_loop:utterance",
                                         utt_data, NULL);
                    cJSON_Delete(utt_data);
                    s_state = SAT_THINKING;
                }
            }
        }
    }

    /* Hub-side events. */
    if (strcmp(type, "recognizer_loop:wakeword") == 0) {
        s_state = SAT_WAKE_DETECTED;
    }
    if (strcmp(type, "recognizer_loop:utterance") == 0) {
        s_state = SAT_THINKING;
    }
    if (strcmp(type, "recognizer_loop:audio_output_end") == 0 ||
        strcmp(type, "ovos.utterance.handled") == 0) {
        if (s_state == SAT_SPEAKING || s_state == SAT_THINKING) {
            s_state = SAT_IDLE;
        }
    }
}

static void on_binary(hm_client_t *client, hm_bin_type_t bin_type,
                       const uint8_t *data, size_t len)
{
    if (bin_type == HM_BIN_TTS_AUDIO && s_tts == TTS_HM_BINARY) {
        ESP_LOGD(TAG, "TTS binary: %zu bytes", len);
        s_state = SAT_SPEAKING;

        const int16_t *pcm;
        size_t samples;

        if (audio_is_wav(data, len)) {
            /* WAV-wrapped PCM — extract data chunk. */
            uint32_t sr = 0;
            if (audio_wav_extract_pcm(data, len, &pcm, &samples, &sr, NULL)) {
                /* Copy to mutable buffer for volume scaling. */
                int16_t *buf = malloc(samples * sizeof(int16_t));
                if (buf) {
                    memcpy(buf, pcm, samples * sizeof(int16_t));
                    audio_apply_volume(buf, samples, rotary_get_volume());
                    i2s_spk_push(buf, samples);
                    free(buf);
                }
            }
        } else {
            /* Raw PCM — assume 16-bit mono. */
            samples = len / sizeof(int16_t);
            int16_t *buf = malloc(samples * sizeof(int16_t));
            if (buf) {
                memcpy(buf, data, len);
                audio_apply_volume(buf, samples, rotary_get_volume());
                i2s_spk_push(buf, samples);
                free(buf);
            }
        }
    }
}

static void on_state_change(hm_client_t *client, hm_state_t state)
{
    ESP_LOGI(TAG, "HiveMind state: %d", state);

    if (state == HM_STATE_READY) {
        /* Connection (re)established — reset pipeline state. */
        s_streaming = false;
        s_rec_count = 0;
        s_state = SAT_IDLE;
        ESP_LOGI(TAG, "Connected — listen=%s stt=%s tts=%s lang=%s",
                 s_listen == LISTEN_VAD_ONLY ? "vad" : "wakeword",
                 s_stt == STT_HM_BINARY ? "hm-bin" :
                 s_stt == STT_HM_B64    ? "hm-b64" : "http",
                 s_tts == TTS_HM_BINARY ? "hm-bin" :
                 s_tts == TTS_HM_B64    ? "hm-b64" : "http",
                 s_lang);
    } else if (state == HM_STATE_DISCONNECTED) {
        /* Connection lost — stop any in-progress session cleanly.
         * Audio tasks check s_state and will pause on their own.
         * Recording buffer is discarded (partial audio is useless). */
        ESP_LOGW(TAG, "Disconnected from hub — will auto-reconnect");
        s_streaming = false;
        s_rec_count = 0;
        s_state = SAT_ERROR;
    }
}

/* ── Session helpers ─────────────────────────────────────────────── */

static void stt_session_start(void)
{
    if (s_streaming) return;
    s_streaming = true;
    s_state = SAT_LISTENING;
    s_rec_count = 0;
    hm_send_bus_message(s_client, "recognizer_loop:record_begin", NULL, NULL);
    ESP_LOGI(TAG, "STT session started");
}

static void stt_session_stop(void)
{
    if (!s_streaming) return;
    s_streaming = false;
    hm_send_bus_message(s_client, "recognizer_loop:record_end", NULL, NULL);

    /* For batch STT modes, submit the recording now. */
    if (s_stt == STT_HM_B64 || s_stt == STT_HTTP) {
        submit_stt_recording();
    } else {
        s_state = SAT_IDLE;
    }
    s_rec_count = 0;
    ESP_LOGI(TAG, "STT session ended");
}

/* ══════════════════════════════════════════════════════════════════
 *  LISTEN_VAD_ONLY — continuous VAD, stream or accumulate audio
 * ══════════════════════════════════════════════════════════════════ */

static void vad_only_task(void *arg)
{
    const size_t CHUNK = 480;
    int16_t buf[480];
    bool in_speech = false;
    int silence_ms = 0;

    ESP_LOGI(TAG, "VAD-only task started");

    while (1) {
        if (hm_client_get_state(s_client) != HM_STATE_READY) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (button_is_muted()) {
            if (in_speech) {
                in_speech = false;
                silence_ms = 0;
                if (s_streaming) stt_session_stop();
            }
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        size_t samples_read = 0;
        if (i2s_mic_read(buf, CHUNK, &samples_read) != ESP_OK) continue;
        if (samples_read < CHUNK) continue;

        bool speech = vad_is_speech(buf, CHUNK);

        if (speech) {
            if (!in_speech) {
                in_speech = true;
                stt_session_start();
            }
            silence_ms = 0;
        }

        if (in_speech) {
            if (s_stt == STT_HM_BINARY) {
                /* Stream raw chunks directly to hub. */
                hm_send_binary(s_client, HM_BIN_RAW_AUDIO,
                               (const uint8_t *)buf,
                               CHUNK * sizeof(int16_t));
            } else {
                /* Accumulate in recording buffer for batch submission. */
                if (s_rec_buf && s_rec_count + CHUNK <= REC_MAX_SAMPLES) {
                    memcpy(s_rec_buf + s_rec_count, buf,
                           CHUNK * sizeof(int16_t));
                    s_rec_count += CHUNK;
                }
            }

            if (!speech) {
                silence_ms += VP_VAD_FRAME_MS;
                if (silence_ms >= VP_MAX_SILENCE_MS) {
                    in_speech = false;
                    silence_ms = 0;
                    stt_session_stop();
                }
            }
        }
    }
}

/* ══════════════════════════════════════════════════════════════════
 *  LISTEN_WAKE_WORD — ESP-SR WakeNet + VAD pipeline
 * ══════════════════════════════════════════════════════════════════ */

static void wake_word_task(void *arg)
{
    size_t feed_chunk = speech_detect_feed_chunksize();
    size_t fetch_chunk = speech_detect_fetch_chunksize();

    int16_t *mic_buf = malloc(feed_chunk * sizeof(int16_t));
    int16_t *out_buf = malloc(fetch_chunk * sizeof(int16_t));
    if (!mic_buf || !out_buf) {
        ESP_LOGE(TAG, "Failed to allocate audio buffers");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Wake word task started (ESP-SR WakeNet + VAD)");

    while (1) {
        if (hm_client_get_state(s_client) != HM_STATE_READY) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (button_is_muted()) {
            if (s_streaming) stt_session_stop();
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        size_t samples_read = 0;
        if (i2s_mic_read(mic_buf, feed_chunk, &samples_read) != ESP_OK) continue;
        if (samples_read < feed_chunk) continue;

        speech_detect_feed(mic_buf, feed_chunk);

        size_t out_samples = 0;
        speech_event_t evt = speech_detect_fetch(out_buf, &out_samples);

        switch (evt) {
            case SPEECH_EVT_WAKEWORD:
                ESP_LOGI(TAG, "Wake word detected!");
                s_state = SAT_WAKE_DETECTED;
                stt_session_start();
                break;
            case SPEECH_EVT_VAD_START:
                if (!s_streaming) stt_session_start();
                break;
            case SPEECH_EVT_VAD_END:
                ESP_LOGI(TAG, "VAD: speech ended");
                stt_session_stop();
                break;
            case SPEECH_EVT_NONE:
                break;
        }

        /* During session: stream or accumulate processed audio. */
        if (s_streaming && out_samples > 0) {
            if (s_stt == STT_HM_BINARY) {
                hm_send_binary(s_client, HM_BIN_STT_HANDLE,
                               (const uint8_t *)out_buf,
                               out_samples * sizeof(int16_t));
            } else if (s_rec_buf) {
                size_t remaining = REC_MAX_SAMPLES - s_rec_count;
                size_t to_copy = out_samples < remaining ? out_samples : remaining;
                if (to_copy > 0) {
                    memcpy(s_rec_buf + s_rec_count, out_buf,
                           to_copy * sizeof(int16_t));
                    s_rec_count += to_copy;
                }
                if (s_rec_count >= REC_MAX_SAMPLES) {
                    ESP_LOGW(TAG, "Recording buffer full");
                    stt_session_stop();
                }
            }
        }
    }
}

/* ── UI task (button + mute + LED) ───────────────────────────────── */

static void ui_task(void *arg)
{
    bool btn_listening = false;

    while (1) {
        if (button_is_muted()) {
            if (s_state != SAT_MUTED) {
                s_state = SAT_MUTED;
                btn_listening = false;
                led_set_state(SAT_MUTED);
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if (button_was_pressed()) {
            btn_listening = !btn_listening;
            if (btn_listening) {
                stt_session_start();
            } else {
                stt_session_stop();
            }
        }
        if (btn_listening && !s_streaming) btn_listening = false;

        /* Volume change — log it (LED volume display could be added). */
        uint8_t vol;
        if (rotary_volume_changed(&vol)) {
            ESP_LOGI(TAG, "Volume: %d%%", vol);
        }

        if (s_state == SAT_SPEAKING && i2s_spk_is_idle()) {
            s_state = SAT_IDLE;
        }

        led_set_state(s_state);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

/* ── Entry point ─────────────────────────────────────────────────── */

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(example_connect());

    /* Hardware. */
    ESP_ERROR_CHECK(codec_init_all());
    ESP_ERROR_CHECK(i2s_mic_init());
    ESP_ERROR_CHECK(i2s_spk_init());
    ESP_ERROR_CHECK(led_ring_init());
    ESP_ERROR_CHECK(button_init());
    ESP_ERROR_CHECK(rotary_init());

    /* Wake word mode requires ESP-SR. */
    if (s_listen == LISTEN_WAKE_WORD) {
        ESP_ERROR_CHECK(speech_detect_init());
    }

    /* Batch STT modes need a recording buffer. */
    if (s_stt == STT_HM_B64 || s_stt == STT_HTTP) {
        s_rec_buf = heap_caps_malloc(REC_MAX_SAMPLES * sizeof(int16_t),
                                      MALLOC_CAP_SPIRAM);
        if (!s_rec_buf) {
            s_rec_buf = malloc(REC_MAX_SAMPLES * sizeof(int16_t));
        }
        if (!s_rec_buf) {
            ESP_LOGE(TAG, "FATAL: cannot allocate recording buffer");
        }
    }

    /* OVOS HTTP servers. */
#ifdef CONFIG_EXAMPLE_STT_HTTP
    ovos_http_set_servers(CONFIG_EXAMPLE_OVOS_STT_URL, NULL);
#endif
#ifdef CONFIG_EXAMPLE_TTS_HTTP
    ovos_http_set_servers(NULL, CONFIG_EXAMPLE_OVOS_TTS_URL);
#endif

    i2s_spk_start_task();
    led_set_state(SAT_IDLE);

    /* HiveMind client. */
    hm_config_t config = {
        .host             = CONFIG_EXAMPLE_HIVEMIND_HOST,
        .port             = 5678,
        .username         = "voice-pe",
        .access_key       = CONFIG_EXAMPLE_HIVEMIND_KEY,
        .password         = CONFIG_EXAMPLE_HIVEMIND_PASSWORD,
        .site_id          = "voice-pe-satellite",
        .preferred_cipher = HM_CIPHER_AES_GCM,
        .reconnect_ms     = CONFIG_HIVEMIND_RECONNECT_MS,
    };

    ESP_ERROR_CHECK(hm_client_init(&s_client, &config));
    hm_client_set_bus_cb(s_client, on_bus_message);
    hm_client_set_binary_cb(s_client, on_binary);
    hm_client_set_state_cb(s_client, on_state_change);

    ESP_LOGI(TAG, "Connecting to HiveMind hub at %s:%d", config.host, config.port);
    ESP_ERROR_CHECK(hm_client_connect(s_client));

    /* Start listening task based on mode. */
    if (s_listen == LISTEN_VAD_ONLY) {
        xTaskCreate(vad_only_task, "vad_sat", 4096, NULL, 5, NULL);
    } else {
        xTaskCreate(wake_word_task, "ww_sat", 8192, NULL, 5, NULL);
    }

    xTaskCreate(ui_task, "ui", 4096, NULL, 4, NULL);
}
