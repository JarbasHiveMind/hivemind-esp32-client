/**
 * @file unity_config.h
 * @brief Unity test framework configuration for host builds.
 *
 * Configures Unity to output to stdout/stderr and use standard C functions.
 */

#ifndef UNITY_CONFIG_H
#define UNITY_CONFIG_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Use standard C library functions */
#define UNITY_OUTPUT_COLOR
#define UNITY_OUTPUT_START() printf("\n")
#define UNITY_OUTPUT_FLUSH() fflush(stdout)

/* Memory functions */
#define UNITY_MALLOC(size) malloc(size)
#define UNITY_FREE(ptr) free(ptr)
#define UNITY_CALLOC(count, size) calloc(count, size)

/* ESP-IDF Unity extension — TEST_CASE macro.
 * On the host we expand it to a plain C function named test_line_<N>.
 * To avoid collisions between test files that happen to have TEST_CASE
 * on the same line, the CMake build uses -D to set TEST_FILE_ID per TU. */
#define _TC_CAT(a, b) a##b
#define _TC_CAT_(a, b) _TC_CAT(a, b)
#ifndef TEST_FILE_ID
#define TEST_FILE_ID x
#endif
#define TEST_CASE(desc, group) \
    void _TC_CAT_(_TC_CAT_(test_, TEST_FILE_ID), _TC_CAT_(_, __LINE__))(void)

#ifdef __cplusplus
}
#endif

#endif /* UNITY_CONFIG_H */
