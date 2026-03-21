# Voice PE Satellite Documentation

HiveMind satellite firmware for the Home Assistant Voice Preview Edition (ESP32-S3).

## Contents

- **[Getting Started](getting-started.md)** — Install ESP-IDF, configure, build, flash, and use. Start here.
- **[Architecture](architecture.md)** — Hardware diagram, audio pipeline, task layout, memory map, pin reference.

## Quick Reference

**Build:**
```bash
cd examples/voice_pe_satellite
idf.py set-target esp32s3
idf.py menuconfig
idf.py -p /dev/ttyACM0 flash monitor
```

**Configuration axes** (all independent, set in menuconfig):
- Listening: VAD-only or ESP-SR wake word
- STT: HiveMind binary, HiveMind base64, or OVOS HTTP
- TTS: HiveMind binary, HiveMind base64, or OVOS HTTP

**Source files:**

| File | Purpose |
|------|---------|
| `main.c` | Entry point, mode dispatch, callbacks |
| `speech_detect.c` | ESP-SR WakeNet + VAD wrapper |
| `vad_simple.c` | Energy-based VAD (mic-satellite mode) |
| `ovos_http.c` | OVOS STT/TTS HTTP client |
| `codec_init.c` | I2C + XMOS + AIC3204 init |
| `i2s_mic.c` | Microphone I2S input |
| `i2s_spk.c` | Speaker I2S output + upsample |
| `led_ring.c` | WS2812 LED state display |
| `button.c` | Button + mute switch |
| `voice_pe_hw.h` | All pin defs + config enums |
