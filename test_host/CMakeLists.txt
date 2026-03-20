cmake_minimum_required(VERSION 3.10)
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
FetchContent_MakeAvailable(unity)
set(UNITY_SOURCE_DIR ${unity_SOURCE_DIR})

# ================================================================
# Main test executable
# ================================================================

add_executable(test_host_runner
    main.c
    ../test/test_crypto.c
    ../test/test_binary.c
    ../components/hivemind/src/hivemind_crypto.c
    ../components/hivemind/src/hivemind_binary.c
)

# ================================================================
# Include directories
# ================================================================

target_include_directories(test_host_runner PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}              # For esp_stubs.h, unity_config.h
    ${CMAKE_CURRENT_SOURCE_DIR}/..           # For relative paths
    ../components/hivemind/include           # For hivemind headers
    ${unity_SOURCE_DIR}/src                  # For unity.h
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

target_compile_options(test_host_runner PRIVATE
    -Wall -Wextra -Wpedantic
    -Wno-unused-function                     # Unity may define unused macros
    -O2
)

# ================================================================
# Output
# ================================================================

set_target_properties(test_host_runner PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
)
