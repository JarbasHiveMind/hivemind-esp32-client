# hivemind-esp32-client

ESP-IDF client library for connecting ESP32 devices as HiveMind satellites. Supports encrypted WebSocket communication with AES-GCM and ChaCha20-Poly1305.

## Build

```bash
idf.py build
idf.py flash monitor
```

## Examples

- [`examples/text_satellite/`](examples/text_satellite/) — text-only satellite, sends utterances and logs responses
- [`examples/mic_satellite/`](examples/mic_satellite/) — audio satellite with INMP441 I2S mic input

## License

Apache 2.0
