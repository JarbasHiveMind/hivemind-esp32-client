/**
 * @file ovos_http.c
 * @brief Client for OVOS STT and TTS HTTP servers.
 *
 * STT: POST raw WAV to /stt → plain text transcript
 * TTS: GET /tts?utterance=X&lang=Y → WAV audio response
 *
 * WAV format helpers handle building the header for STT requests
 * and stripping it from TTS responses.
 */
#include "ovos_http.h"

#include "esp_http_client.h"
#include "esp_log.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static const char *TAG = "ovos_http";

static char s_stt_url[256] = "";
static char s_tts_url[256] = "";

/* ── WAV header helpers ──────────────────────────────────────────── */

/** 44-byte WAV header for 16-bit mono 16 kHz PCM. */
static void wav_header(uint8_t *hdr, uint32_t data_bytes)
{
    uint32_t file_size = data_bytes + 36;
    uint32_t sample_rate = 16000;
    uint16_t channels = 1;
    uint16_t bits = 16;
    uint32_t byte_rate = sample_rate * channels * bits / 8;
    uint16_t block_align = channels * bits / 8;

    memcpy(hdr, "RIFF", 4);
    memcpy(hdr + 4, &file_size, 4);
    memcpy(hdr + 8, "WAVEfmt ", 8);
    uint32_t fmt_size = 16;
    memcpy(hdr + 16, &fmt_size, 4);
    uint16_t fmt_pcm = 1;
    memcpy(hdr + 20, &fmt_pcm, 2);
    memcpy(hdr + 22, &channels, 2);
    memcpy(hdr + 24, &sample_rate, 4);
    memcpy(hdr + 28, &byte_rate, 4);
    memcpy(hdr + 32, &block_align, 2);
    memcpy(hdr + 34, &bits, 2);
    memcpy(hdr + 36, "data", 4);
    memcpy(hdr + 40, &data_bytes, 4);
}

/** Find the "data" chunk in a WAV file and return offset to PCM data. */
static int wav_find_data(const uint8_t *wav, size_t wav_len, size_t *data_offset,
                          size_t *data_size)
{
    if (wav_len < 44) {
        return -1;
    }
    /* Simple: assume standard 44-byte header. */
    if (memcmp(wav, "RIFF", 4) != 0 || memcmp(wav + 8, "WAVE", 4) != 0) {
        return -1;
    }
    /* Walk chunks to find "data". */
    size_t pos = 12;
    while (pos + 8 <= wav_len) {
        uint32_t chunk_size;
        memcpy(&chunk_size, wav + pos + 4, 4);
        if (memcmp(wav + pos, "data", 4) == 0) {
            *data_offset = pos + 8;
            *data_size = chunk_size;
            return 0;
        }
        pos += 8 + chunk_size;
        if (pos % 2 != 0) pos++;  /* WAV chunks are word-aligned. */
    }
    return -1;
}

/* ── HTTP response buffer ────────────────────────────────────────── */

typedef struct {
    uint8_t *buf;
    size_t len;
    size_t cap;
} http_buf_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_buf_t *rb = (http_buf_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && rb) {
        if (rb->len + evt->data_len <= rb->cap) {
            memcpy(rb->buf + rb->len, evt->data, evt->data_len);
            rb->len += evt->data_len;
        }
    }
    return ESP_OK;
}

/* ── Public API ──────────────────────────────────────────────────── */

void ovos_http_set_servers(const char *stt_url, const char *tts_url)
{
    if (stt_url) {
        strncpy(s_stt_url, stt_url, sizeof(s_stt_url) - 1);
    }
    if (tts_url) {
        strncpy(s_tts_url, tts_url, sizeof(s_tts_url) - 1);
    }
    ESP_LOGI(TAG, "OVOS servers: STT=%s TTS=%s", s_stt_url, s_tts_url);
}

esp_err_t ovos_http_stt(const int16_t *pcm16k_mono, size_t num_samples,
                         const char *lang, char *out_text, size_t text_size)
{
    if (s_stt_url[0] == '\0') {
        ESP_LOGE(TAG, "STT server URL not configured");
        return ESP_ERR_INVALID_STATE;
    }

    /* Build WAV in memory: 44-byte header + PCM data. */
    size_t data_bytes = num_samples * sizeof(int16_t);
    size_t wav_size = 44 + data_bytes;
    uint8_t *wav = malloc(wav_size);
    if (!wav) {
        return ESP_ERR_NO_MEM;
    }
    wav_header(wav, data_bytes);
    memcpy(wav + 44, pcm16k_mono, data_bytes);

    /* Build URL with lang parameter. */
    char url[512];
    snprintf(url, sizeof(url), "%s?lang=%s", s_stt_url, lang);

    /* Response buffer for transcript text. */
    http_buf_t resp = {
        .buf = (uint8_t *)out_text,
        .len = 0,
        .cap = text_size - 1,
    };

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event_handler,
        .user_data = &resp,
        .timeout_ms = 30000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_http_client_set_header(client, "Content-Type", "audio/wav");
    esp_http_client_set_post_field(client, (const char *)wav, wav_size);

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    free(wav);

    if (err != ESP_OK || status != 200) {
        ESP_LOGE(TAG, "STT request failed: err=%d status=%d", err, status);
        out_text[0] = '\0';
        return ESP_FAIL;
    }

    out_text[resp.len] = '\0';
    ESP_LOGI(TAG, "STT result: \"%s\"", out_text);
    return ESP_OK;
}

esp_err_t ovos_http_tts(const char *utterance, const char *lang,
                         int16_t *out_pcm, size_t max_samples,
                         size_t *out_samples)
{
    if (s_tts_url[0] == '\0') {
        ESP_LOGE(TAG, "TTS server URL not configured");
        *out_samples = 0;
        return ESP_ERR_INVALID_STATE;
    }

    /* Build URL with query parameters. */
    char url[1024];
    snprintf(url, sizeof(url), "%s?utterance=%s&lang=%s", s_tts_url, utterance, lang);

    /* Allocate buffer for WAV response (up to 512 KB). */
    size_t wav_cap = 512 * 1024;
    uint8_t *wav_buf = malloc(wav_cap);
    if (!wav_buf) {
        *out_samples = 0;
        return ESP_ERR_NO_MEM;
    }

    http_buf_t resp = {
        .buf = wav_buf,
        .len = 0,
        .cap = wav_cap,
    };

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .event_handler = http_event_handler,
        .user_data = &resp,
        .timeout_ms = 30000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200) {
        ESP_LOGE(TAG, "TTS request failed: err=%d status=%d", err, status);
        free(wav_buf);
        *out_samples = 0;
        return ESP_FAIL;
    }

    /* Extract PCM from WAV response. */
    size_t data_offset, data_size;
    if (wav_find_data(wav_buf, resp.len, &data_offset, &data_size) != 0) {
        ESP_LOGE(TAG, "TTS response is not valid WAV");
        free(wav_buf);
        *out_samples = 0;
        return ESP_FAIL;
    }

    size_t pcm_samples = data_size / sizeof(int16_t);
    if (pcm_samples > max_samples) {
        pcm_samples = max_samples;
    }
    memcpy(out_pcm, wav_buf + data_offset, pcm_samples * sizeof(int16_t));
    *out_samples = pcm_samples;

    ESP_LOGI(TAG, "TTS audio: %zu samples", pcm_samples);
    free(wav_buf);
    return ESP_OK;
}
