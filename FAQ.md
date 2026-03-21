# FAQ — hivemind-esp32-client

**Q: What ciphers are supported?**
A: AES-256-GCM and ChaCha20-Poly1305. Negotiated during handshake via `ciphers` field in SHAKE message. Enum: `hm_cipher_t` — `hivemind_crypto.h:19`.

**Q: How long does PBKDF2 key derivation take on ESP32?**
A: 10-30 seconds (100,000 iterations, `HM_PBKDF2_ITERATIONS` — `hivemind_crypto.h:31`). One-time cost per connection. AES is HW-accelerated via mbedTLS on ESP32.

**Q: What is the minimum ESP-IDF version?**
A: ESP-IDF 5.0+. Requires `esp_websocket_client`, mbedTLS, and cJSON components.

**Q: How do I add this as a component?**
A: Copy `components/hivemind/` into your project's `components/` directory, or use the ESP Component Registry.

**Q: What is the binary protocol format?**
A: V1 bitstring: `pad(1) + versioned(1) + [version(8)] + type(5) + compressed(1) + metalen(8) + meta + [bintype(4)] + payload`. See `hivemind_binary.h:1-8`.

**Q: What are the fixed buffer sizes?**
A: Protocol uses 4096-byte buffers for envelopes and plaintext (`hivemind_protocol.c:216,239,433`). May be insufficient for large messages.

**Q: Does it support TLS?**
A: Not yet. V1 scope uses unencrypted WebSocket with application-layer AEAD encryption.

**Q: Can I use this with a Home Assistant Voice PE?**
A: Yes. The `examples/voice_pe_satellite/` example targets the Voice PE board (ESP32-S3 + XMOS Voice Kit + AIC3204 DAC). It uses push-to-talk via the center button, plays TTS through the speaker, and shows state on the LED ring. The XMOS codec must be pre-flashed with its firmware (no DFU from ESP-IDF). Build with `idf.py set-target esp32s3`.

**Q: What sample rate conversion does the Voice PE speaker use?**
A: TTS arrives as 16 kHz 16-bit mono. The speaker driver triplicates each sample to reach 48 kHz, zero-extends to 32-bit, and duplicates to stereo for the AIC3204 DAC. Adequate for speech; not suitable for music.

**Q: What modes does the Voice PE satellite support?**
A: Three independent axes configured via `idf.py menuconfig`: (1) Listening: VAD-only or ESP-SR wake word. (2) STT: HiveMind binary streaming, HiveMind base64 batch, or OVOS HTTP server. (3) TTS: HiveMind binary, HiveMind base64, or OVOS HTTP server. Any combination works.

**Q: Does the Voice PE example support wake word detection?**
A: Yes, in wake word mode. Uses ESP-SR WakeNet9 "hi_esp". In VAD-only mode, wake word is handled by the hub. Center button works as manual push-to-talk in both modes.

**Q: How do I use OVOS STT/TTS HTTP servers?**
A: Set STT and/or TTS transport to "OVOS HTTP" in menuconfig, then configure the server URLs. STT: `POST /stt` with `audio/wav` body. TTS: `GET /tts?utterance=X&lang=Y`. See `ovos_http.c`.

**Q: Can I use a custom wake word instead of "hi_esp"?**
A: Change `cfg.wakenet_model_name` in `speech_detect.c`. Built-in options include "hi_esp", "alexa", "hi_lexin". Custom wake words require Espressif's model training service.

**Q: What's the difference between HM binary and HM base64 STT?**
A: Binary streams raw PCM chunks in real-time (`HM_BIN_RAW_AUDIO`) — lowest latency. Base64 records the entire utterance, wraps as WAV, and sends via `recognizer_loop:b64_transcribe` — higher latency but simpler hub-side processing.
