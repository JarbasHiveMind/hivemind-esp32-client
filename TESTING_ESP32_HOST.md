# ESP32 C Module Host Testing Guide

## Overview

The `test_host/` directory enables native Linux compilation and testing of ESP32 C modules (`test_crypto.c`, `test_binary.c`) **without ESP-IDF or hardware**. Uses:
- **MbedTLS**: Cryptography library (same as ESP-IDF uses)
- **Unity**: C unit test framework (same as ESP32 firmware tests)
- **CMake**: Cross-platform build system

---

## Arch Linux Setup

### 1. Install System Dependencies

```bash
# MbedTLS (cryptography library)
sudo pacman -S mbedtls

# CMake (build system)
sudo pacman -S cmake

# Build tools (if not already installed)
sudo pacman -S base-devel

# Verify installations
pacman -Q mbedtls cmake base-devel
```

### 2. Build and Run Tests

```bash
cd hivemind-esp32-client

# Create isolated build directory
cmake -B build test_host/

# Build test runner
cmake --build build

# Run tests
./build/test_host_runner
```

**Expected Output**:
```
[I] Running test_host_runner...
[I] test_crypto.c: ✓ PASS
[I] test_binary.c: ✓ PASS
...
Tests run: XX, Failures: 0, Ignores: 0
```

---

## Troubleshooting

### Issue: CMake can't find MbedTLS headers

**Error**:
```
CMake Error: MbedTLS not found
```

**Solution**:
```bash
# Verify MbedTLS installation
pacman -Q mbedtls
ls -la /usr/include/mbedtls/

# If not found, reinstall
sudo pacman -S --needed mbedtls

# Rebuild
rm -rf build/
cmake -B build test_host/
cmake --build build
```

### Issue: Missing headers for ESP stubs

**Error**:
```c
fatal error: unistd.h: No such file or directory
```

**Solution**: Install POSIX development headers:
```bash
sudo pacman -S glibc
```

### Issue: CMake Ninja/Make not found

**Error**:
```
Could not find supported make program
```

**Solution**:
```bash
# Use default generator (Unix Makefiles on Arch)
cmake -B build test_host/ -G "Unix Makefiles"
cmake --build build
```

### Issue: Unity framework download fails

**Error**:
```
fatal: unable to access 'https://github.com/ThrowTheSwitch/Unity.git'
```

**Solution**:
```bash
# Check network
ping -c 1 github.com

# Manually download and use local copy (optional)
git clone https://github.com/ThrowTheSwitch/Unity.git /tmp/unity
cmake -B build test_host/ -DUNITY_SOURCE_DIR=/tmp/unity
```

---

## Understanding the Build Process

### Files Overview

| File | Purpose |
|------|---------|
| `CMakeLists.txt` | Finds MbedTLS, downloads Unity, compiles test runner |
| `esp_stubs.h` | ESP-IDF compatibility shims (logging, random, errors) |
| `unity_config.h` | Unity framework configuration |
| `main.c` | Test runner entry point |
| `../test/test_crypto.c` | Crypto unit tests (included as source) |
| `../test/test_binary.c` | Binary protocol unit tests (included) |
| `../components/hivemind/src/` | Production crypto/binary implementations |

### Build Flow

```
CMakeLists.txt
  ├── find_package(MbedTLS) → /usr/lib/libmbedcrypto.so
  ├── FetchContent(unity) → downloads v2.5.2 from GitHub
  ├── add_executable(test_host_runner)
  │   ├── main.c
  │   ├── test/test_crypto.c (via #include)
  │   ├── test/test_binary.c (via #include)
  │   ├── components/hivemind/src/hivemind_crypto.c
  │   └── components/hivemind/src/hivemind_binary.c
  ├── target_include_directories()
  │   ├── test_host/ (for esp_stubs.h, unity_config.h)
  │   ├── components/hivemind/include/
  │   └── ${unity_SOURCE_DIR}/src
  └── target_link_libraries(MbedTLS::mbedcrypto, m)

Execution:
  ./build/test_host_runner
    ├── UNITY_BEGIN()
    ├── Discover all TEST_CASE blocks
    ├── Run each test
    ├── Report results
    └── UNITY_END()
```

---

## What the Tests Cover

### test_crypto.c

Validates HiveMind crypto layer using MbedTLS:
- **HSUB generation**: Key derivation with PBKDF2 + SHA256
- **IV extraction**: Parsing initiation vectors from encoded format
- **Key derivation**: Session key from client/server IVs
- **AES-GCM encryption**: Full roundtrip encrypt/decrypt
- **ChaCha20-Poly1305**: Alternative cipher support
- **Encoding formats**: HEX, Base64, URL-safe B64, Base32, Z85 variants

### test_binary.c

Validates HiveMind binary protocol:
- **Message encoding**: HiveMessage → binary wire format
- **Message decoding**: Binary → JSON objects
- **Type handling**: BUS, BROADCAST, ESCALATE, BINARY messages
- **Metadata**: Session, source, destination routing
- **Payloads**: String, bytes, nested structures

---

## Manual Compilation (Advanced)

If CMake doesn't work, compile manually:

```bash
# Download Unity headers
git clone https://github.com/ThrowTheSwitch/Unity.git /tmp/unity

# Compile with gcc
gcc -std=c99 -Wall \
    -I/tmp/unity/src \
    -I/usr/include \
    -Ihivemind-esp32-client/components/hivemind/include \
    -Ihivemind-esp32-client/test_host \
    -c /tmp/unity/src/unity.c -o /tmp/unity.o

gcc -std=c99 -Wall \
    -I/tmp/unity/src \
    -I/usr/include \
    -Ihivemind-esp32-client/components/hivemind/include \
    -Ihivemind-esp32-client/test_host \
    -c hivemind-esp32-client/test/test_crypto.c -o /tmp/test_crypto.o

gcc -std=c99 -Wall \
    -I/tmp/unity/src \
    -I/usr/include \
    -Ihivemind-esp32-client/components/hivemind/include \
    -Ihivemind-esp32-client/test_host \
    -c hivemind-esp32-client/components/hivemind/src/hivemind_crypto.c -o /tmp/hivemind_crypto.o

# Link
gcc -o /tmp/test_runner \
    /tmp/unity.o /tmp/test_crypto.o /tmp/hivemind_crypto.o \
    -lmbedcrypto -lm

# Run
/tmp/test_runner
```

---

## CI/CD Integration (GitHub Actions)

`.github/workflows/tests.yml` runs automatically:

```yaml
jobs:
  c-host-tests:
    runs-on: ubuntu-latest  # Debian-based
    steps:
      - apt-get install libmbedtls-dev cmake build-essential
      - cmake -B build test_host/
      - cmake --build build
      - ./build/test_host_runner
```

**Note**: GitHub Actions uses Debian/Ubuntu, so dependency names differ:
- **Arch**: `mbedtls` → **Debian**: `libmbedtls-dev`
- **Arch**: `base-devel` → **Debian**: `build-essential`

---

## Validation Checklist

```bash
#!/bin/bash
set -e

echo "=== ESP32 C Host Test Validation ==="

# 1. System dependencies
echo -n "Checking MbedTLS... "
pacman -Q mbedtls > /dev/null && echo "✓" || (echo "✗ Install: pacman -S mbedtls" && exit 1)

echo -n "Checking CMake... "
which cmake > /dev/null && echo "✓" || (echo "✗ Install: pacman -S cmake" && exit 1)

echo -n "Checking GCC... "
which gcc > /dev/null && echo "✓" || (echo "✗ Install: pacman -S base-devel" && exit 1)

# 2. MbedTLS headers
echo -n "Checking MbedTLS headers... "
ls /usr/include/mbedtls/aes.h > /dev/null && echo "✓" || (echo "✗ Reinstall: pacman -S mbedtls" && exit 1)

# 3. Build test runner
echo "Building test runner..."
cd hivemind-esp32-client
rm -rf build/
cmake -B build test_host/ > /dev/null 2>&1
cmake --build build > /dev/null 2>&1
echo "✓ Build successful"

# 4. Run tests
echo "Running tests..."
if ./build/test_host_runner 2>&1 | grep -q "PASS\|OK"; then
    echo "✓ Tests passed"
else
    echo "✗ Tests failed"
    ./build/test_host_runner
    exit 1
fi

echo ""
echo "=== All ESP32 validation checks passed! ==="
```

Run it:
```bash
chmod +x validate_esp32.sh
./validate_esp32.sh
```

---

## Key Differences: Host vs ESP32

| Aspect | Host (Linux) | ESP32 |
|--------|------|--------|
| **Compiler** | GCC (gcc) | Xtensa (xtensa-esp32-elf-gcc) |
| **Target** | x86-64 Linux | ARM ESP32 microcontroller |
| **MbedTLS** | System package | ESP-IDF built-in component |
| **Random** | `getrandom(2)` POSIX | `esp_fill_random()` ESP-IDF |
| **Logging** | `fprintf(stdout)` | `ESP_LOG*` macros |
| **Headers** | Standard C library | ESP-IDF + FreeRTOS |
| **Unit Test Framework** | Unity v2.5.2 | Same Unity (via ESP-IDF) |

**Why Host Build?**
- ✅ Fast iteration (no flash/reboot cycle)
- ✅ CI/CD friendly (no hardware/emulator needed)
- ✅ Crypto validation independent of ESP firmware
- ✅ Early catch of portability issues

---

## See Also

- `test_host/CMakeLists.txt` — Build configuration
- `test_host/esp_stubs.h` — POSIX shims
- `test/test_crypto.c` — Crypto test cases
- `test/test_binary.c` — Binary protocol test cases
- `.github/workflows/tests.yml` — CI/CD job definition
