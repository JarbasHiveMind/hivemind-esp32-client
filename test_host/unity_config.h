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
#define UNITY_INCLUDE_CONFIG_H
#define UNITY_OUTPUT_COLOR
#define UNITY_OUTPUT_START() printf("\n")
#define UNITY_OUTPUT_FLUSH() fflush(stdout)

/* Memory functions */
#define UNITY_MALLOC(size) malloc(size)
#define UNITY_FREE(ptr) free(ptr)
#define UNITY_CALLOC(count, size) calloc(count, size)

#ifdef __cplusplus
}
#endif

#endif /* UNITY_CONFIG_H */
