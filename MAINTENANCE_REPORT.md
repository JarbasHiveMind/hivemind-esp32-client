# Maintenance Report — hivemind-esp32-client

## 2026-03-21

- **AI Model**: Claude Opus 4.6
- **Actions Taken**: Added `examples/voice_pe_satellite/` for HA Voice PE (ESP32-S3). Three independent config axes: listening (VAD-only / ESP-SR wake word), STT (HM binary / HM base64 / OVOS HTTP), TTS (HM binary / HM base64 / OVOS HTTP). Includes: XMOS Voice Kit mic, AIC3204 DAC speaker, WS2812 LEDs, button, mute switch, ESP-SR WakeNet9, energy VAD, OVOS HTTP STT/TTS client. Updated all docs.
- **Oversight**: Human-approved plan with iterative feedback. AI-generated code. Requires on-device verification.

## 2026-03-20

- **AI Model**: Claude Opus 4.6
- **Actions Taken**: Initial implementation of complete HiveMind ESP32 client — crypto layer (AES-GCM, ChaCha20-Poly1305, PBKDF2, hsub), protocol FSM (handshake state machine), binary V1 bitstring codec, client lifecycle management, examples, and unit tests. Created `docs/index.md`, `FAQ.md`, `AUDIT.md`, `SUGGESTIONS.md`.
- **Oversight**: Human-reviewed plan, AI-generated code.

- **AI Model**: Gemini 2.0 Flash
- **Actions Taken**: Created detailed user-facing documentation in `/docs` directory including `getting-started.md`, `api-reference.md`, `examples.md`, and `troubleshooting.md`. Updated `index.md` with current feature set and minimal example. Added source code citations to all new documentation files.
- **Oversight**: AI-generated content following strict workspace rules.
