/**
 * @file voice_pe_hw.h
 * @brief Pin definitions, hardware constants, and configuration enums for the
 *        Home Assistant Voice PE board running as a HiveMind satellite.
 *
 * Hardware: ESP32-S3 + XMOS Voice Kit (mic codec) + TI AIC3204 (speaker DAC)
 *           + 12x WS2812 LED ring + center button + mute switch + rotary encoder.
 */
#ifndef VOICE_PE_HW_H
#define VOICE_PE_HW_H

/* ── I2C (internal bus: XMOS + AIC3204) ──────────────────────────── */
#define VP_I2C_PORT          I2C_NUM_0
#define VP_I2C_SDA           5
#define VP_I2C_SCL           6
#define VP_I2C_FREQ_HZ       400000

/* ── XMOS Voice Kit codec ────────────────────────────────────────── */
#define VP_XMOS_I2C_ADDR     0x42
#define VP_XMOS_RESET_PIN    4

/* ── I2S Microphone input (from XMOS) ────────────────────────────── */
#define VP_MIC_I2S_PORT      I2S_NUM_0
#define VP_MIC_BCLK          13
#define VP_MIC_LRCLK         14
#define VP_MIC_DIN           15
#define VP_MIC_SAMPLE_RATE   16000
#define VP_MIC_BIT_DEPTH     32
#define VP_MIC_DMA_BUF_COUNT 4
#define VP_MIC_DMA_BUF_LEN   1024

/* ── I2S Speaker output (to AIC3204 DAC) ─────────────────────────── */
#define VP_SPK_I2S_PORT      I2S_NUM_1
#define VP_SPK_BCLK          8
#define VP_SPK_LRCLK         7
#define VP_SPK_DOUT          10
#define VP_SPK_SAMPLE_RATE   48000
#define VP_SPK_BIT_DEPTH     32
#define VP_SPK_AMP_EN        47
#define VP_SPK_DMA_BUF_COUNT 4
#define VP_SPK_DMA_BUF_LEN   1024

/* ── TI AIC3204 DAC ─────────────────────────────────────────────── */
#define VP_AIC3204_I2C_ADDR  0x18

/* ── WS2812 LED ring ─────────────────────────────────────────────── */
#define VP_LED_PIN           21
#define VP_LED_COUNT         12
#define VP_LED_POWER_PIN     45

/* ── Controls ────────────────────────────────────────────────────── */
#define VP_BTN_PIN           0
#define VP_MUTE_PIN          3

/* ── Rotary encoder (deferred) ───────────────────────────────────── */
#define VP_DIAL_A            16
#define VP_DIAL_B            18

/* ── Listening mode (VAD / Wake Word) ────────────────────────────── */

/**
 * Controls how the device decides when to listen for speech.
 *
 * LISTEN_VAD_ONLY:
 *   Streams raw audio to hub whenever VAD detects speech. Hub handles WW.
 *   (Mirrors hivemind-mic-satellite)
 *
 * LISTEN_WAKE_WORD:
 *   ESP-SR WakeNet on device. Only listens after wake word. VAD detects end.
 *   (Mirrors HiveMind-voice-relay)
 */
typedef enum {
    LISTEN_VAD_ONLY,    /* Continuous VAD; WW on hub */
    LISTEN_WAKE_WORD,   /* Local WW (ESP-SR WakeNet) + VAD */
} listen_mode_t;

/* ── STT transport ───────────────────────────────────────────────── */

/**
 * Controls how speech audio reaches the STT engine.
 *
 * STT_HM_BINARY:
 *   Stream raw PCM chunks via HM_BIN_RAW_AUDIO or HM_BIN_STT_HANDLE.
 *   Hub runs STT on the audio stream. (Default for VAD-only mode)
 *
 * STT_HM_B64:
 *   Record entire utterance, base64-encode as WAV, send via
 *   recognizer_loop:b64_transcribe. Hub returns transcription.
 *   (Default for wake word mode)
 *
 * STT_HTTP:
 *   POST WAV to OVOS STT HTTP server. Device handles STT directly.
 */
typedef enum {
    STT_HM_BINARY,   /* Stream raw audio chunks to hub */
    STT_HM_B64,      /* Batch base64 WAV to hub */
    STT_HTTP,         /* POST to OVOS STT HTTP server */
} stt_mode_t;

/* ── TTS transport ───────────────────────────────────────────────── */

/**
 * Controls how TTS audio is obtained.
 *
 * TTS_HM_BINARY:
 *   Hub sends TTS as HM_BIN_TTS_AUDIO binary frames. (Default)
 *
 * TTS_HM_B64:
 *   Request via speak:b64_audio, receive speak:b64_audio.response.
 *
 * TTS_HTTP:
 *   GET from OVOS TTS HTTP server. Device fetches audio directly.
 */
typedef enum {
    TTS_HM_BINARY,   /* Hub pushes binary audio chunks */
    TTS_HM_B64,      /* Request/response base64 WAV via bus */
    TTS_HTTP,         /* GET from OVOS TTS HTTP server */
} tts_mode_t;

/* ── Satellite state (drives LEDs) ───────────────────────────────── */
typedef enum {
    SAT_IDLE,
    SAT_WAKE_DETECTED,
    SAT_LISTENING,
    SAT_THINKING,
    SAT_SPEAKING,
    SAT_ERROR,
    SAT_MUTED,
} sat_state_t;

/* ── Audio streaming constants ───────────────────────────────────── */
#define VP_MAX_SILENCE_MS    6000
#define VP_VAD_FRAME_MS      30

#endif /* VOICE_PE_HW_H */
