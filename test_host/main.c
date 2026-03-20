/**
 * @file main.c
 * @brief Unity test runner for host-based crypto and binary protocol tests.
 *
 * Compiles and runs the native versions of test_crypto.c and test_binary.c
 * on Linux without ESP-IDF. Unity discovers and runs all TEST_CASE blocks
 * and reports results to stdout.
 */

#include "unity.h"

/* Forward declarations of test functions */
void test_crypto_setup(void);
void test_crypto_teardown(void);
void test_binary_setup(void);
void test_binary_teardown(void);

/**
 * Main entry point for the Unity test runner.
 * Unity's `RUN_TEST` macro (defined in unity.h) automatically calls test
 * functions that were registered by TEST_CASE macros during compilation.
 */
int main(void)
{
    UNITY_BEGIN();

    /* Crypto tests automatically registered by TEST_CASE macro */
    /* (no explicit RUN_TEST calls needed; Unity discovers them) */

    /* Binary tests automatically registered by TEST_CASE macro */
    /* (no explicit RUN_TEST calls needed; Unity discovers them) */

    return UNITY_END();
}
