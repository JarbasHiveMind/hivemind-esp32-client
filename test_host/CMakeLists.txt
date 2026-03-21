cmake_minimum_required(VERSION 3.10)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "Allow older CMake in FetchContent deps")
project(hivemind_host_tests C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ================================================================
# Find required dependencies
# ================================================================

# Try to find MbedTLS via standard CMake config or system paths
find_package(MbedTLS QUIET CONFIG)

# If not found via config, try finding headers manually
if(NOT MbedTLS_FOUND AND NOT mbedtls_FOUND)
    message(STATUS "MbedTLS not found via CMake config, trying manual search...")

    # Check common installation paths
    find_path(MBEDTLS_INCLUDE_DIR
        NAMES mbedtls/sha256.h
        PATHS /usr/include /usr/local/include /opt/homebrew/include
        NO_DEFAULT_PATH
    )

    find_library(MBEDCRYPTO_LIB
        NAMES mbedcrypto
        PATHS /usr/lib /usr/local/lib /opt/homebrew/lib /usr/lib/x86_64-linux-gnu
        NO_DEFAULT_PATH
    )

    if(MBEDTLS_INCLUDE_DIR AND MBEDCRYPTO_LIB)
        message(STATUS "Found mbedTLS: ${MBEDTLS_INCLUDE_DIR}")
        set(MbedTLS_FOUND TRUE)
        add_library(MbedTLS::mbedcrypto UNKNOWN IMPORTED)
        set_target_properties(MbedTLS::mbedcrypto PROPERTIES
            IMPORTED_LOCATION "${MBEDCRYPTO_LIB}"
            INTERFACE_INCLUDE_DIRECTORIES "${MBEDTLS_INCLUDE_DIR}"
        )
    else()
        message(FATAL_ERROR
            "MbedTLS not found. Install libmbedtls-dev (Ubuntu/Debian) or "
            "libmbedtls-devel (Fedora/RHEL), or set CMAKE_PREFIX_PATH to point to MbedTLS installation."
        )
    endif()
endif()

# ================================================================
# Download and prepare Unity framework
# ================================================================

include(FetchContent)
FetchContent_Declare(unity
    GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
    GIT_TAG v2.5.2
)
FetchContent_Declare(cjson
    GIT_REPOSITORY https://github.com/DaveGamble/cJSON.git
    GIT_TAG v1.7.18
)
FetchContent_MakeAvailable(unity)
# Populate cJSON source only (don't build — its CMakeLists conflicts with Unity)
FetchContent_GetProperties(cjson)
if(NOT cjson_POPULATED)
    FetchContent_Populate(cjson)
endif()
set(UNITY_SOURCE_DIR ${unity_SOURCE_DIR})

# ================================================================
# Main test executable
# ================================================================

add_executable(test_host_runner
    main.c
    ../test/test_crypto.c
    ../test/test_binary.c
    ../test/test_protocol.c
    ../test/test_vad_simple.c
    ../test/test_wav_header.c
    ../components/hivemind/src/hivemind_crypto.c
    ../components/hivemind/src/hivemind_binary.c
    ../components/hivemind/src/hivemind_protocol.c
    ../examples/voice_pe_satellite/main/vad_simple.c
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
set_source_files_properties(../test/test_vad_simple.c PROPERTIES
    COMPILE_DEFINITIONS "TEST_FILE_ID=vad"
)
set_source_files_properties(../test/test_wav_header.c PROPERTIES
    COMPILE_DEFINITIONS "TEST_FILE_ID=wav"
)

# ================================================================
# Output
# ================================================================

set_target_properties(test_host_runner PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
