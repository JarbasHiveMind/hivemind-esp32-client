/**
 * @file ovos_http.h
 * @brief Client for OVOS STT and TTS HTTP servers.
 *
 * OVOS exposes STT and TTS as simple HTTP REST endpoints:
 *   - STT: POST /stt  (audio/wav body → text/plain response)
 *   - TTS: GET  /tts?utterance=...&lang=...  (→ audio/wav response)
 *
 * Used in MODE_VOICE_SATELLITE when STT/TTS is handled locally
 * via network servers rather than through the HiveMind hub.
 */
#ifndef OVOS_HTTP_H
#define OVOS_HTTP_H

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/**
 * @brief Configure OVOS HTTP server endpoints.
 *
 * @param stt_url  STT server URL (e.g. "http://192.168.1.100:8080/stt").
 * @param tts_url  TTS server URL (e.g. "http://192.168.1.100:9666/tts").
 */
void ovos_http_set_servers(const char *stt_url, const char *tts_url);

/**
 * @brief Send audio to OVOS STT server and get transcription.
 *
 * Posts raw WAV audio to the STT endpoint. Returns the transcribed text.
 *
 * @param pcm16k_mono  16-bit signed PCM samples at 16 kHz.
 * @param num_samples  Number of samples.
 * @param lang         Language code (e.g. "en-us").
 * @param out_text     Output buffer for transcription text.
 * @param text_size    Size of output buffer.
 * @return ESP_OK on success, ESP_FAIL on HTTP or parse error.
 */
esp_err_t ovos_http_stt(const int16_t *pcm16k_mono, size_t num_samples,
                         const char *lang, char *out_text, size_t text_size);

/**
 * @brief Request TTS audio from OVOS TTS server.
 *
 * Fetches synthesized audio for the given utterance. Returns raw PCM
 * (16-bit mono 16 kHz) after stripping the WAV header.
 *
 * @param utterance    Text to synthesize.
 * @param lang         Language code (e.g. "en-us").
 * @param out_pcm      Output buffer for PCM samples.
 * @param max_samples  Maximum samples that fit in out_pcm.
 * @param out_samples  Actual number of samples written.
 * @return ESP_OK on success, ESP_FAIL on HTTP error.
 */
esp_err_t ovos_http_tts(const char *utterance, const char *lang,
                         int16_t *out_pcm, size_t max_samples,
                         size_t *out_samples);

#endif /* OVOS_HTTP_H */
