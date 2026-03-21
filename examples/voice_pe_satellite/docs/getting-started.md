# Voice PE Satellite — Getting Started Guide

Turn your Home Assistant Voice Preview Edition into a HiveMind satellite. This guide assumes no prior ESP32 or HiveMind experience.

---

## What You Need

### Hardware
- **Home Assistant Voice Preview Edition** (the physical device)
- **USB-C cable** (for flashing firmware)
- **Computer** (Linux, macOS, or Windows with WSL2)

### Software (you'll install these)
- **ESP-IDF v5.2+** (Espressif's development framework)
- **Git**
- **Python 3.8+** (ESP-IDF needs it)

### Network
- **WiFi network** (2.4 GHz — the ESP32-S3 doesn't support 5 GHz)
- **HiveMind hub** running somewhere on your network (see [HiveMind setup](https://github.com/JarbasHiveMind/hivemind-core))
- **(Optional)** OVOS STT/TTS servers if using HTTP mode

---

## Step 1: Install ESP-IDF

ESP-IDF is Espressif's official development framework. You need it to compile firmware for the ESP32-S3.

### Linux / macOS

```bash
# Install prerequisites
# Ubuntu/Debian:
sudo apt install git wget flex bison gperf python3 python3-pip python3-venv \
  cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0

# macOS:
brew install cmake ninja dfu-util python3

# Clone ESP-IDF (v5.2 or later)
mkdir -p ~/esp
cd ~/esp
git clone -b v5.2.3 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf

# Install the ESP32-S3 toolchain
./install.sh esp32s3

# Activate ESP-IDF (you'll need this in every terminal session)
. ./export.sh
```

### Windows (WSL2)

Use WSL2 with Ubuntu and follow the Linux instructions above.

### Verify Installation

```bash
idf.py --version
# Should print something like: ESP-IDF v5.2.3
```

---

## Step 2: Clone the HiveMind ESP32 Client

```bash
cd ~/esp  # or wherever you keep projects
git clone https://github.com/JarbasHiveMind/hivemind-esp32-client.git
cd hivemind-esp32-client/examples/voice_pe_satellite
```

---

## Step 3: Configure the Firmware

### Set the Target Chip

```bash
idf.py set-target esp32s3
```

### Open the Configuration Menu

```bash
idf.py menuconfig
```

This opens a text-based menu. Navigate with arrow keys, Enter to select, Esc to go back.

### Configure WiFi

Go to: **Example Connection Configuration**
- **WiFi SSID**: Your 2.4 GHz network name
- **WiFi Password**: Your network password

### Configure HiveMind Connection

Go to: **HiveMind Voice PE Satellite → HiveMind Connection**
- **HiveMind hub hostname or IP**: The IP address of your HiveMind hub (e.g., `192.168.1.100`)
- **HiveMind access key**: The access key from your hub (you get this when registering a satellite)
- **HiveMind password**: The password from your hub

### Choose Your Listening Mode

Go to: **HiveMind Voice PE Satellite → Listening Mode**

| Option | What It Does | Best For |
|--------|-------------|----------|
| **VAD only** | Device detects speech, hub detects wake word | Simplest setup, no wake word model on device |
| **Wake word + VAD** | Device listens for "Hi ESP" then records | Privacy-conscious, lower network traffic |

**Recommendation for beginners**: Start with **VAD only** — it's simpler and doesn't need ESP-SR models.

### Choose Your STT Transport

Go to: **HiveMind Voice PE Satellite → STT Transport**

| Option | What It Does | Best For |
|--------|-------------|----------|
| **HiveMind binary** | Streams audio chunks to hub in real-time | Default, lowest latency |
| **HiveMind base64** | Records full utterance, sends as one message | Compatibility with voice-relay hubs |
| **OVOS HTTP** | Sends audio to a separate STT server | When you run your own STT server |

**Recommendation for beginners**: Use **HiveMind binary** — it just works with any hub.

### Choose Your TTS Transport

Go to: **HiveMind Voice PE Satellite → TTS Transport**

| Option | What It Does | Best For |
|--------|-------------|----------|
| **HiveMind binary** | Hub sends audio chunks | Default, works everywhere |
| **HiveMind base64** | Hub sends base64-encoded WAV | Compatibility with voice-relay hubs |
| **OVOS HTTP** | Device fetches audio from TTS server | When you run your own TTS server |

**Recommendation for beginners**: Use **HiveMind binary**.

### Save and Exit

Press `S` to save, then `Esc` until you exit.

---

## Step 4: Build the Firmware

```bash
idf.py build
```

This takes 5-15 minutes the first time (it downloads ESP-SR models and compiles everything). Subsequent builds are much faster.

**If the build fails**, common issues:
- Missing ESP-IDF setup: Run `. ~/esp/esp-idf/export.sh` first
- Wrong target: Run `idf.py set-target esp32s3` if you see "wrong chip" errors

---

## Step 5: Flash the Voice PE

### Connect the Device

1. Plug your Voice PE into your computer via USB-C
2. Find the serial port:
   ```bash
   # Linux:
   ls /dev/ttyUSB* /dev/ttyACM*
   # macOS:
   ls /dev/cu.usb*
   ```
   Note the port name (e.g., `/dev/ttyACM0`)

### Put the Device in Flash Mode

The Voice PE should enter flash mode automatically. If it doesn't:
1. Hold the center button
2. Plug in the USB cable (or press the reset button if accessible)
3. Release the center button after 1 second

### Flash

```bash
idf.py -p /dev/ttyACM0 flash
```

Replace `/dev/ttyACM0` with your actual port.

### Monitor Serial Output

```bash
idf.py -p /dev/ttyACM0 monitor
```

You should see:
```
I (xxx) voice_pe: Connecting to HiveMind hub at 192.168.1.100:5678
I (xxx) voice_pe: HiveMind state: 5
I (xxx) voice_pe: Connected — listen=vad stt=hm-bin tts=hm-bin
```

Press `Ctrl+]` to exit the monitor.

### Shortcut: Build + Flash + Monitor in One Command

```bash
idf.py -p /dev/ttyACM0 flash monitor
```

---

## Step 6: Use It

### If Using VAD-Only Mode
Just start talking. The device's LED ring will turn **blue** when it detects speech and starts streaming to the hub. The hub will detect the wake word in the audio stream.

### If Using Wake Word Mode
Say **"Hi ESP"**. The LED ring turns **cyan** (wake word detected), then **blue** (listening). Speak your command. When you stop talking, the device sends the audio for processing.

### LED Colors Reference

| Color | Meaning |
|-------|---------|
| **Dim white** | Ready, waiting |
| **Cyan** | Wake word detected |
| **Blue** | Listening / recording |
| **Purple** | Processing (waiting for response) |
| **Green** | Speaking (playing TTS response) |
| **Orange** | Muted (slide the mute switch to unmute) |
| **Red** | Error (check serial monitor for details) |

### Center Button
Press the center button to manually start/stop recording. This works in any mode as a push-to-talk override.

### Mute Switch
Slide the mute switch on the side of the device to disable the microphone completely. LEDs turn orange.

---

## Troubleshooting

### "XMOS codec NOT detected"
The XMOS voice processing chip on the Voice PE needs its own firmware. If you've only ever used the device with Home Assistant, it should already be flashed. If you see this warning but audio still works, ignore it.

### "AIC3204 NOT detected"
The speaker codec isn't responding. Check that the I2C bus is working. This usually means a hardware issue or the device isn't a genuine Voice PE.

### No audio output
1. Check that the speaker amplifier is enabled (GPIO47 — handled automatically)
2. Verify TTS mode matches your hub configuration
3. Check serial monitor for "TTS audio chunk" messages — if present, the issue is in the I2S/codec config

### WiFi won't connect
- Ensure you're using a 2.4 GHz network (ESP32-S3 doesn't support 5 GHz)
- Check SSID/password in menuconfig
- Try moving the device closer to the router

### HiveMind won't connect
1. Verify the hub is running: `curl http://<hub-ip>:5678` should respond
2. Check access key and password match what the hub expects
3. Look for "PBKDF2" in serial output — key derivation takes 10-30 seconds, be patient

### Build fails with "esp-sr" errors
ESP-SR is a large component (~78 MB). If download fails:
1. Check internet connection
2. Delete `managed_components/` and try again: `rm -rf managed_components/ && idf.py build`

---

## What's Next

- **Change the wake word**: Edit `speech_detect.c`, change `cfg.wakenet_model_name` (options: `"wn9_hiesp"`, `"wn9_alexa"`, `"wn9_hilexin"`)
- **Set up OVOS STT/TTS servers**: Run [ovos-stt-server](https://github.com/OpenVoiceOS/ovos-stt-http-server) and [ovos-tts-server](https://github.com/OpenVoiceOS/ovos-tts-server) on your LAN, then select "OVOS HTTP" in menuconfig
- **Customize LED colors**: Edit `led_ring.c`, modify the RGB values in `led_set_state()`
- **Adjust silence timeout**: Change `VP_MAX_SILENCE_MS` in `voice_pe_hw.h` (default: 6000 ms)
- **Adjust VAD sensitivity**: Change `VAD_ENERGY_THRESHOLD` in `vad_simple.c` (lower = more sensitive)
