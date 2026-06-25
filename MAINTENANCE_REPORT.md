# Maintenance Report — hivemind-esp32-client

## 2026-06-25

- **AI Model**: Claude Opus 4.8
- **Actions Taken**: CI/dependency modernization. Fixed the failing host-test job
  (the distro `libmbedtls-dev` on Ubuntu 24.04 does not export
  `mbedtls_pkcs5_pbkdf2_hmac_ext`) by building mbedTLS from source pinned to v3.6.6
  (the ESP-IDF 5.x LTS line) via FetchContent, removing the system mbedTLS dependency.
  Added an `idf-build` CI job that compiles every example with the official
  `espressif/esp-idf-ci-action@v1` against ESP-IDF v5.4 (text/mic → esp32, voice_pe →
  esp32s3). Added `components/hivemind/idf_component.yml` declaring the
  `espressif/esp_websocket_client` managed dependency (no longer bundled in IDF 5.x)
  and an `idf >= 5.1` floor. Added the missing `Kconfig.projbuild` and
  `EXTRA_COMPONENT_DIRS`/`REQUIRES` wiring for the text and mic examples so they build
  standalone. Refreshed README/docs/FAQ for the managed dependency, ESP-IDF version,
  and the test story. Verified all 69 host unit tests pass locally with the new build.
- **Protocol V1 conformance**: audited the C crypto/binary/handshake against the
  canonical `hivemind_bus_client` Python reference — CONFORMS (PBKDF2 100k/SHA256/XOR
  salt, 8-byte IV + 48-hex hSub, AES-GCM 16-byte nonce, ChaCha20 12-byte nonce, 16-byte
  tags, `ciphertext`/`tag`/`nonce` envelope, all 7 encodings, binary bitstring layout
  and message types 0-12). No code changes required for conformance.
- **Oversight**: AI-generated. Host tests verified locally; ESP-IDF firmware builds
  verified by CI only (no local IDF toolchain). On-device end-to-end remains a
  documented manual procedure.

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
