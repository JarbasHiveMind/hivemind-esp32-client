/**
 * @file test_vad_simple.c
 * @brief Unit tests for the energy-based VAD module.
 */
#include "unity.h"
#include <string.h>
#include <math.h>

/* Forward declare from vad_simple.c — we include the source directly. */
#include "../examples/voice_pe_satellite/main/vad_simple.h"

TEST_CASE("vad detects silence in zero buffer", "[vad]")
{
    int16_t buf[480];
    memset(buf, 0, sizeof(buf));
    TEST_ASSERT_FALSE(vad_is_speech(buf, 480));
}

TEST_CASE("vad detects speech in loud signal", "[vad]")
{
    int16_t buf[480];
    /* Fill with a ~1000 amplitude signal (well above threshold of 200). */
    for (int i = 0; i < 480; i++) {
        buf[i] = (int16_t)(1000 * sin(2.0 * 3.14159 * 440.0 * i / 16000.0));
    }
    TEST_ASSERT_TRUE(vad_is_speech(buf, 480));
}

TEST_CASE("vad detects silence in low noise", "[vad]")
{
    int16_t buf[480];
    /* Fill with very low amplitude noise (~50, below threshold of 200). */
    for (int i = 0; i < 480; i++) {
        buf[i] = (int16_t)(50 * sin(2.0 * 3.14159 * 300.0 * i / 16000.0));
    }
    TEST_ASSERT_FALSE(vad_is_speech(buf, 480));
}

TEST_CASE("vad handles empty buffer", "[vad]")
{
    int16_t buf[1] = {0};
    TEST_ASSERT_FALSE(vad_is_speech(buf, 0));
}

TEST_CASE("vad detects speech at threshold boundary", "[vad]")
{
    /* RMS of constant value K is K itself. At threshold=200, value 250 should trigger. */
    int16_t buf[480];
    for (int i = 0; i < 480; i++) {
        buf[i] = 250;
    }
    TEST_ASSERT_TRUE(vad_is_speech(buf, 480));
}

TEST_CASE("vad silence at threshold boundary", "[vad]")
{
    /* Constant value 100 → RMS=100 < 200 threshold → silence. */
    int16_t buf[480];
    for (int i = 0; i < 480; i++) {
        buf[i] = 100;
    }
    TEST_ASSERT_FALSE(vad_is_speech(buf, 480));
}

TEST_CASE("vad handles single sample speech", "[vad]")
{
    int16_t buf[1] = {10000};
    TEST_ASSERT_TRUE(vad_is_speech(buf, 1));
}

TEST_CASE("vad handles single sample silence", "[vad]")
{
    int16_t buf[1] = {10};
    TEST_ASSERT_FALSE(vad_is_speech(buf, 1));
}
