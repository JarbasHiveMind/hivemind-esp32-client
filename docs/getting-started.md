# Getting Started

From a bare ESP32 to a HiveMind satellite that exchanges messages with a hub.

## What this component does

A HiveMind **satellite** captures input on an edge device and forwards it to a central **hub**. The hub runs the AI reasoning — intent parsing, skills, text-to-speech — and sends responses back. This ESP-IDF component implements the satellite side in C: the encrypted HiveMind handshake and bus/binary messaging, small enough to run on an ESP32.

```
ESP32 (this component)  ⇄  hivemind-core hub  ⇄  OVOS skills
```

## Prerequisites

### Build host

- **ESP-IDF 5.1+** (CI builds against v5.4). Install it and source the environment:

  ```bash
  git clone https://github.com/espressif/esp-idf.git
  cd esp-idf && ./install.sh && source export.sh
  ```

### Hardware

- An ESP32 (or ESP32-S3 for the Voice PE example) with Wi-Fi.
- For the mic example: an INMP441 I2S microphone.

### Hub

- A running HiveMind hub ([hivemind-core](https://github.com/JarbasHiveMind/HiveMind-core)) reachable at a known address and port (default `5678`).
- A client credential (username, access key, password) registered on the hub.

## Step 1 — Stand up a hub

On a desktop or home server:

```bash
pip install hivemind-core
hivemind-core listen
```

The hub listens on port `5678` by default.

## Step 2 — Register the satellite

```bash
hivemind-core add-client --name esp32 \
  --access-key "your-access-key" --password "your-password"
```

Keep the access key and password — the device authenticates with them. List clients with `hivemind-core list-clients`.

## Step 3 — Build and flash an example

The component lives in `components/hivemind/`; the examples already reference it.

```bash
cd examples/text_satellite
idf.py set-target esp32          # or esp32s3, esp32c3
idf.py menuconfig
#   Example Connection Configuration -> Wi-Fi SSID / Password
#   HiveMind -> host, access key, password
idf.py build
idf.py flash -p /dev/ttyUSB0     # your serial port
idf.py monitor
```

## Step 4 — Confirm it connects

The monitor output shows the Wi-Fi IP, the connection, and the round-trip:

```
I (4210) text_sat: Connecting to 192.168.1.100:5678
I (9880) text_sat: Connected! Sending utterance...
I (10120) text_sat: TTS response: It is half past three.
```

The first connection includes PBKDF2 key derivation, which takes 10-30 s on-device; reconnects are fast.

## Using the component in your own app

Add `components/hivemind/` to your project's `components/` directory, then drive it through the public API in `hivemind.h`:

1. Fill an `hm_config_t` with the hub address and credentials.
2. `hm_client_init`, register callbacks (`hm_client_set_bus_cb`, `hm_client_set_binary_cb`, `hm_client_set_state_cb`).
3. `hm_client_connect`.
4. Send with `hm_send_utterance`, `hm_send_bus_message`, `hm_send_binary`.

See [examples.md](examples.md) for full snippets.

## Next steps

- [Configuration & credentials reference](configuration.md)
- [Architecture and full API](index.md)
- [Examples](examples.md)
- [Integration testing against a live hub](integration-testing.md)
