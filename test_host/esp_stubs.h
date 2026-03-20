/**
 * @file esp_stubs.h
 * @brief ESP-IDF stub header for host-based testing.
 *
 * Provides minimal implementations of ESP-IDF functions and macros required
 * for compiling and testing hivemind crypto/binary on native Linux without
 * ESP-IDF or hardware. Uses standard C library and POSIX functions.
 */

#ifndef ESP_STUBS_H
#define ESP_STUBS_H

#include <stdio.h>
#include <unistd.h>
#include <sys/random.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * Error codes
 * ================================================================ */

typedef int esp_err_t;

#define ESP_OK          0
#define ESP_FAIL        -1
#define ESP_ERR_NO_MEM  -2
#define ESP_ERR_INVALID_ARG -3

/* ================================================================
 * Random number generation
 * ================================================================ */

/**
 * Fill buffer with random bytes using getrandom(2).
 * @param buf Destination buffer
 * @param len Number of bytes to fill
 * @return 0 on success, -1 on error
 */
static inline esp_err_t esp_fill_random(void *buf, size_t len)
{
    if (getrandom(buf, len, 0) == (ssize_t)len) {
        return ESP_OK;
    }
    return ESP_FAIL;
}

/* ================================================================
 * Logging macros
 * ================================================================ */

#define ESP_LOGE(tag, ...) do { \
    fprintf(stderr, "[E] [%s] ", tag); \
    fprintf(stderr, __VA_ARGS__); \
    fprintf(stderr, "\n"); \
} while (0)

#define ESP_LOGW(tag, ...) do { \
    fprintf(stdout, "[W] [%s] ", tag); \
    fprintf(stdout, __VA_ARGS__); \
    fprintf(stdout, "\n"); \
} while (0)

#define ESP_LOGI(tag, ...) do { \
    fprintf(stdout, "[I] [%s] ", tag); \
    fprintf(stdout, __VA_ARGS__); \
    fprintf(stdout, "\n"); \
} while (0)

#define ESP_LOGD(tag, ...) do { \
    (void)0; \
} while (0)

/* ================================================================
 * Memory allocation
 * ================================================================ */

#define heap_caps_malloc(size, caps) malloc(size)
#define heap_caps_free(ptr) free(ptr)

#ifdef __cplusplus
}
#endif

#endif /* ESP_STUBS_H */
