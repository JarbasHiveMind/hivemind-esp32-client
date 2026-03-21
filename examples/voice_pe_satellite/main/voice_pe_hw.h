/**
 * @file voice_pe_hw.h
 * @brief Pin definitions and hardware constants for the Home Assistant Voice PE board.
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
#define VP_MIC_BIT_DEPTH     32   /* XMOS outputs 32-bit; we extract 16-bit */
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
#define VP_AIC3204_I2C_ADDR  0x18  /* 7-bit address (ADDR pin low) */

/* ── WS2812 LED ring ─────────────────────────────────────────────── */
#define VP_LED_PIN           21
#define VP_LED_COUNT         12
#define VP_LED_POWER_PIN     45

/* ── Controls ────────────────────────────────────────────────────── */
#define VP_BTN_PIN           0    /* Center button, active low */
#define VP_MUTE_PIN          3    /* Hardware mute switch */

/* ── Rotary encoder (deferred — not used in MVP) ─────────────────── */
#define VP_DIAL_A            16
#define VP_DIAL_B            18

/* ── Satellite state (drives LEDs + audio pipeline logic) ────────── */
typedef enum {
    SAT_IDLE,           /* Connected, listening for wake word. Dim white LEDs. */
    SAT_WAKE_DETECTED,  /* Wake word heard, waiting for speech. Cyan LEDs. */
    SAT_LISTENING,      /* Streaming mic audio to hub (VAD active). Blue LEDs. */
    SAT_SPEAKING,       /* Playing TTS response. Green LEDs. */
    SAT_ERROR,          /* Connection error / init failure. Red LEDs. */
    SAT_MUTED,          /* Hardware mute switch active. Orange LEDs. */
} sat_state_t;

#endif /* VOICE_PE_HW_H */
