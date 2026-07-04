cmake_minimum_required(VERSION 3.16)
# Some pinned FetchContent deps still declare a pre-3.10 minimum; allow them
# to configure under modern CMake without each emitting a hard error.
if(POLICY CMP0048)
    cmake_policy(SET CMP0048 NEW)
endif()
set(CMAKE_POLICY_VERSION_MINIMUM 3.10 CACHE STRING "Floor for FetchContent deps that declare an older minimum")
project(hivemind_host_tests C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ================================================================
# Dependencies — fetched from source and pinned.
#
# We build mbedTLS from source (rather than the distro's libmbedtls-dev)
# so the host tests link against the SAME mbedTLS major.minor that ESP-IDF
# bundles (3.6.x). This guarantees API parity (e.g. the modern
# mbedtls_pkcs5_pbkdf2_hmac_ext symbol, which several distro builds omit)
# and makes the build reproducible on any host with no system mbedTLS.
# ================================================================

include(FetchContent)

# --- mbedTLS (crypto only) — pinned to the ESP-IDF 5.x LTS line ---------
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING  OFF CACHE BOOL "" FORCE)
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(mbedtls
    GIT_REPOSITORY https://github.com/Mbed-TLS/mbedtls.git
    GIT_TAG v3.6.6
    GIT_SHALLOW TRUE
    GIT_SUBMODULES_RECURSE TRUE
)

FetchContent_Declare(unity
    GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
    GIT_TAG v2.5.2
    GIT_SHALLOW TRUE
)
# cJSON: we compile cJSON.c directly into the test runner rather than
# building its library/target. SOURCE_SUBDIR points at a directory with no
# CMakeLists.txt so MakeAvailable downloads the source without configuring
# cJSON's own build (whose options would clash with ours).
FetchContent_Declare(cjson
    GIT_REPOSITORY https://github.com/DaveGamble/cJSON.git
    GIT_TAG v1.7.18
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR do-not-configure
)

FetchContent_MakeAvailable(mbedtls unity cjson)

# mbedTLS exports the imported target `MbedTLS::mbedcrypto`.
if(NOT TARGET MbedTLS::mbedcrypto)
    message(FATAL_ERROR "mbedTLS build did not provide MbedTLS::mbedcrypto target")
endif()

set(UNITY_SOURCE_DIR ${unity_SOURCE_DIR})

# ================================================================
# Main test executable
# ================================================================

add_executable(test_host_runner
    main.c
    ../test/test_crypto.c
    ../test/test_binary.c
    ../test/test_noise.c
    ../test/test_protocol.c
    ../test/test_vad_simple.c
    ../test/test_wav_header.c
    ../test/test_audio_util.c
    ../components/hivemind/src/hivemind_crypto.c
    ../components/hivemind/src/hivemind_noise.c
    ../components/hivemind/src/hivemind_binary.c
    ../components/hivemind/src/hivemind_protocol.c
    ../examples/voice_pe_satellite/main/vad_simple.c
    ../examples/voice_pe_satellite/main/audio_util.c
    ${unity_SOURCE_DIR}/src/unity.c
    ${cjson_SOURCE_DIR}/cJSON.c
)

# ================================================================
# Include directories
# ================================================================

target_include_directories(test_host_runner PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}              # For esp_stubs.h, unity_config.h
    ${CMAKE_CURRENT_SOURCE_DIR}/..           # For relative paths
    ../components/hivemind/include           # For hivemind headers
    ../examples/voice_pe_satellite/main      # For voice_pe module headers
    ${unity_SOURCE_DIR}/src                  # For unity.h
    ${cjson_SOURCE_DIR}                      # For cJSON.h
)

# ================================================================
# Link libraries
# ================================================================

target_link_libraries(test_host_runner
    MbedTLS::mbedcrypto
    m                                        # Math library
)

# ================================================================
# Compiler flags
# ================================================================

target_compile_definitions(test_host_runner PRIVATE
    UNITY_INCLUDE_CONFIG_H                   # Tell unity.h to include unity_config.h
)

target_compile_options(test_host_runner PRIVATE
    -Wall -Wextra -Wpedantic
    -Wno-unused-function                     # Unity may define unused macros
    -O2
)

# Give each test file a unique TEST_FILE_ID to avoid symbol collisions
# when TEST_CASE macros appear on the same line in different files.
set_source_files_properties(../test/test_binary.c PROPERTIES
    COMPILE_DEFINITIONS "TEST_FILE_ID=bin"
)
set_source_files_properties(../test/test_crypto.c PROPERTIES
    COMPILE_DEFINITIONS "TEST_FILE_ID=crypto"
)
set_source_files_properties(../test/test_protocol.c PROPERTIES
    COMPILE_DEFINITIONS "TEST_FILE_ID=proto"
)
set_source_files_properties(../test/test_noise.c PROPERTIES
    COMPILE_DEFINITIONS "TEST_FILE_ID=noise"
)
set_source_files_properties(../test/test_vad_simple.c PROPERTIES
    COMPILE_DEFINITIONS "TEST_FILE_ID=vad"
)
set_source_files_properties(../test/test_wav_header.c PROPERTIES
    COMPILE_DEFINITIONS "TEST_FILE_ID=wav"
)
set_source_files_properties(../test/test_audio_util.c PROPERTIES
    COMPILE_DEFINITIONS "TEST_FILE_ID=autil"
)

# ================================================================
# Output
# ================================================================

set_target_properties(test_host_runner PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
