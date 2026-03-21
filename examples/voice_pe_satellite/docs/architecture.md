# Voice PE Satellite — Architecture

## Hardware Block Diagram

```
                          ESP32-S3 (Voice PE Board)
┌──────────────────────────────────────────────────────────────────┐
│                                                                  │
│  ┌──────────┐  I2S RX   ┌───────────┐   I2S TX   ┌──────────┐  │
│  │  XMOS    │──────────→│  ESP32-S3  │───────────→│ AIC3204  │  │
│  │ Voice Kit│  16kHz     │           │  48kHz      │   DAC    │  │
│  │ (mic+AEC)│  32-bit    │  ┌─────┐  │  32-bit     │          │  │
│  └──────────┘  stereo    │  │ESP-SR│  │  stereo     └────┬─────┘  │
│       ↑                  │  │WakeN │  │                  │        │
│   [Mics]                 │  │+VAD  │  │             [Speaker]     │
│                          │  └─────┘  │                  │        │
│  ┌──────────┐  I2C       │           │  GPIO21    ┌─────┴────┐  │
│  │ I2C Bus  │←──────────→│           │───────────→│ 12x LED  │  │
│  │ SDA=5    │  400kHz    │           │  WS2812    │   Ring   │  │
│  │ SCL=6    │            │           │            └──────────┘  │
│  └──────────┘            │           │                          │
│                          │           │  GPIO0     [Button]      │
│  ┌──────────┐  WebSocket │           │  GPIO3     [Mute SW]     │
│  │  WiFi    │←──────────→│           │  GPIO47    [Amp EN]      │
│  │ 2.4 GHz  │  :5678     │           │                          │
│  └──────────┘            └───────────┘                          │
└──────────────────────────────────────────────────────────────────┘
         │
         │ WebSocket (HiveMind protocol)
         ↓
   ┌───────────┐
   │  HiveMind │
   │    Hub    │
   └───────────┘
```

## Audio Pipeline

### VAD-Only Mode (LISTEN_VAD_ONLY)

```
I2S Mic (XMOS) → 32-to-16 bit conversion → Energy VAD
                                              │
                              speech?─────yes──→ Stream to hub (RAW_AUDIO)
                                │                 or accumulate (b64/http)
                              no → silence timer
                                     │
                              timeout → stop session
```

### Wake Word Mode (LISTEN_WAKE_WORD)

```
I2S Mic (XMOS) → 32-to-16 bit conversion → ESP-SR AFE
                                              │
                              ┌────────────── Feed
                              ↓
                         ┌─────────┐
                         │ WakeNet │──── "hi_esp" detected ──→ start session
                         │  + VAD  │──── speech ongoing ─────→ stream/accumulate
                         └─────────┘──── silence ────────────→ stop session
                              │
                         processed audio (NS cleaned)
```

## STT/TTS Transport Options

### STT_HM_BINARY (Real-time Streaming)
```
Device ──[HM_BIN_RAW_AUDIO chunks]──→ Hub ──→ STT plugin ──→ utterance
```

### STT_HM_B64 (Batch)
```
Device ──[record full utterance]──→ base64 WAV ──→ Hub
  recognizer_loop:b64_transcribe       ←── recognizer_loop:b64_transcribe.response
```

### STT_HTTP (OVOS Server)
```
Device ──[POST /stt, audio/wav]──→ OVOS STT Server ──→ text
Device ──[recognizer_loop:utterance]──→ Hub (text only)
```

### TTS_HM_BINARY
```
Hub ──[HM_BIN_TTS_AUDIO chunks]──→ Device speaker
```

### TTS_HM_B64
```
Device ──[speak:b64_audio]──→ Hub
Hub ──[speak:b64_audio.response, base64 WAV]──→ Device speaker
```

### TTS_HTTP
```
Hub ──[speak, utterance text]──→ Device
Device ──[GET /tts?utterance=X]──→ OVOS TTS Server ──→ WAV ──→ speaker
```

## FreeRTOS Tasks

| Task | Stack | Priority | Purpose |
|------|-------|----------|---------|
| `vad_sat` or `ww_sat` | 4096 / 8192 | 5 | Audio pipeline (mic read → detection → stream) |
| `spk_play` | 4096 | 5 | Speaker playback (ring buffer → I2S TX) |
| `ui` | 4096 | 4 | Button polling, mute check, LED updates |

## Memory Layout

| Region | Usage |
|--------|-------|
| Internal SRAM | FreeRTOS tasks, I2S DMA buffers, stack |
| PSRAM (8 MB) | Recording buffer (480 KB), ESP-SR models (~1.5 MB), TTS ring buffer (32 KB) |
| Flash (16 MB) | Firmware (3 MB), ESP-SR WakeNet model (~11 MB partition) |

## Pin Map

| GPIO | Function | Direction |
|------|----------|-----------|
| 4 | XMOS reset | Output |
| 5 | I2C SDA | Bidir |
| 6 | I2C SCL | Bidir |
| 7 | Speaker LRCLK | Output |
| 8 | Speaker BCLK | Output |
| 10 | Speaker data | Output |
| 13 | Mic BCLK | Input |
| 14 | Mic LRCLK | Input |
| 15 | Mic data | Input |
| 21 | LED ring data | Output |
| 45 | LED power gate | Output |
| 47 | Speaker amp enable | Output |
| 0 | Center button | Input (active low) |
| 3 | Mute switch | Input |
