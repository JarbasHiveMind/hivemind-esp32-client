# Integration Testing Guide

This guide explains how to build and test the HiveMind ESP32 client with a real hub.

## What is and isn't tested in CI

CI (`.github/workflows/tests.yml`) verifies two things automatically:

1. **Host unit tests** — the crypto layer, the binary V1 codec, the protocol
   handshake FSM, and the Voice PE audio helpers are compiled natively and run as
   Unity tests. The crypto/binary tests include cross-platform interop vectors that
   pin the wire format to HiveMind Protocol V1.
2. **ESP-IDF firmware build** — every example is compiled against a current stable
   ESP-IDF for its target (`text_satellite`/`mic_satellite` → `esp32`,
   `voice_pe_satellite` → `esp32s3`). This is a compile/link check of the real
   firmware, not an execution.

**A full hardware-in-the-loop test against a live hub is NOT run in CI** — it needs
real ESP32 hardware, Wi-Fi, and a running `hivemind-core`. Run the scenarios below
manually on a device when validating an end-to-end change.

## Prerequisites

### Host Requirements
1. **ESP-IDF** — Install v5.0 or later:
   ```bash
   git clone https://github.com/espressif/esp-idf.git
   cd esp-idf && ./install.sh && source export.sh
   ```

2. **HiveMind Hub** — Install and run `hivemind-core`:
   ```bash
   pip install hivemind-core
   hivemind-core listen  # Starts hub on localhost:5678
   ```

3. **Hub Credentials** — Register an ESP32 client:
   ```bash
   hivemind-core add-client --name "esp32-test"
   # Output:
   # Client registered: esp32-test
   # Access Key: <access_key>
   # Password: <password>
   # Note these for sdkconfig
   ```

### ESP32 or Emulator
- **Real device**: ESP32, ESP32-S3, ESP32-C3 with WiFi
- **Emulator**: QEMU (with IDF support) or `idf.py qemu` simulator

## Building Example Applications

### Text Satellite Example

Build and configure for your WiFi:

```bash
cd examples/text_satellite
idf.py set-target esp32  # or esp32s3, esp32c3
idf.py menuconfig
# Configure:
#   WiFi SSID and password
#   HiveMind server host/port
#   Client credentials (username, access_key, password)
#   Site ID
idf.py build
idf.py flash -p /dev/ttyUSB0  # or your serial port
idf.py monitor
```

### Mic Satellite Example

For audio capture (requires I2S microphone):

```bash
cd examples/mic_satellite
idf.py set-target esp32  # ESP32 with I2S support
idf.py menuconfig
# Configure audio input (I2S pins, sample rate)
# Configure HiveMind client credentials (same as above)
idf.py build flash monitor
```

## Integration Test Scenarios

### INT-ESP-01: Connect and Handshake

**Test**: Device connects to hub and completes handshake
**Verification**:
- Serial output shows `[HIVEMIND] Client connected`
- `[HIVEMIND] Handshake complete, state=READY`
- Hub shows device in active clients: `hivemind-core list-clients`

**Expected Output**:
```
[HIVEMIND] Connecting to hub...
[HIVEMIND] WebSocket connected
[HIVEMIND] Received server HELLO
[HIVEMIND] Handshake complete, state=READY
[HIVEMIND] Using cipher: AES-GCM
```

### INT-ESP-02: Send Utterance, Receive Response

**Test**: Text satellite sends "hello" utterance, receives speak response

**Procedure**: the `text_satellite` example sends `"hello world"` automatically
once it reaches the `READY` state (see `examples/text_satellite/main/main.c`).
Watch `idf.py monitor` for the response and check the hub logs to confirm the
message was received.

**Expected Output**:
```
[HIVEMIND] Sending utterance: hello
[HIVEMIND] Received message type: speak
[HIVEMIND] Speech: "Hello, world!"
```

### INT-ESP-03: Stream Audio, Receive TTS Response

**Test**: Mic satellite streams raw audio to hub, receives TTS response

**Procedure**:
1. Start mic satellite
2. Hub receives raw audio via binary protocol
3. Hub (if STT enabled) transcribes audio
4. Hub returns intent matching and TTS response

**Expected Output**:
```
[HIVEMIND] Streaming audio frame 001
[HIVEMIND] Streaming audio frame 002
...
[HIVEMIND] Received message type: speak
[HIVEMIND] Playing audio (TTS response)
```

### INT-ESP-04: Encryption Negotiation

**Test**: Client negotiates cipher with hub

**Configuration**: Set `preferred_cipher` in config:
```c
hm_config_t config = {
    // ...
    .preferred_cipher = HM_CIPHER_CHACHA20_POLY1305,  // Request ChaCha20
};
```

**Expected Output**:
```
[HIVEMIND] Preferred cipher: ChaCha20-Poly1305
[HIVEMIND] Hub selected: ChaCha20-Poly1305
```

### INT-ESP-05: Reconnection After Dropout

**Test**: Device recovers from network dropout

**Procedure**:
1. Device connected and sending messages
2. Interrupt WiFi or network cable
3. Device should attempt reconnection

**Expected Output**:
```
[HIVEMIND] WiFi disconnected
[HIVEMIND] Attempting reconnection (attempt 1/5)...
[HIVEMIND] WiFi reconnected
[HIVEMIND] WebSocket connected
[HIVEMIND] Handshake complete
```

## Performance Baseline

### Memory Usage (Profile with `heap_caps_get_free_size`)
| Component | Typical | Max |
|-----------|---------|-----|
| Client struct | ~2 KB | 4 KB |
| Crypto buffers | ~8 KB | 12 KB |
| JSON parsing | ~4 KB | 8 KB |
| WebSocket buffers | ~16 KB | 32 KB |
| **Total** | **~30 KB** | **~56 KB** |

### Handshake Timing
- PBKDF2 derivation: ~15-20s on ESP32 (100k iterations)
- Full handshake: ~20-25s including network latency
- Subsequent reconnects: ~2-3s (crypto already done)

### Message Throughput
- Text messages: ~100+ messages/sec
- Binary (audio): 16-bit 16kHz mono = 32 KB/s

## Troubleshooting

### Failed to connect
```
[HIVEMIND] WebSocket connection failed: -1
```
- Check WiFi is connected: `idf.py monitor` shows "WiFi connected"
- Verify hub is running: `curl http://<hub_ip>:5679/ping`
- Check firewall (port 5678)

### Handshake timeout
```
[HIVEMIND] Handshake timeout
```
- Verify credentials (username, access_key, password) match registered client
- Check hub logs for "authentication failed"

### Memory exhaustion
```
[HIVEMIND] No memory for message buffer
```
- ESP32 may need heap optimization
- Reduce the WebSocket buffer: `CONFIG_HIVEMIND_WS_BUFFER_SIZE` (HiveMind Client menu)
- Profile with `heap_trace_init_standalone()` to find leaks

### PBKDF2 taking too long
- Expected: ~15-20s on ESP32 at 240 MHz
- Can cache derived key in NVS after first handshake (see SUGGESTIONS.md)

## CI/CD Integration

CI builds every example with the official ESP-IDF action — see
`.github/workflows/tests.yml`. The `idf-build` matrix runs:

```yaml
- uses: espressif/esp-idf-ci-action@v1
  with:
    esp_idf_version: v5.4
    target: esp32        # esp32s3 for voice_pe_satellite
    path: examples/text_satellite
```

The scenarios above (connect/handshake, utterance, audio stream, cipher
negotiation, reconnection) require real hardware and a live hub, so they are run
manually by developers — they are deliberately not part of CI.

## Next Steps

- **Load testing** — Connect 10+ devices simultaneously, measure hub CPU/memory
- **Encoding testing** — Verify all 7 encodings (HEX, B64, B32, Z85B, Z85P, B91) work
- **Error scenarios** — Test graceful handling of hub restart, network partitions
- **OTA updates** — Stream firmware updates via HiveMind binary channel
