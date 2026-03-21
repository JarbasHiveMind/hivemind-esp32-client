/**
 * @file test_audio_util.c
 * @brief Unit tests for audio format detection, WAV parsing, and volume scaling.
 */
#include "unity.h"
#include <string.h>
#include <stdlib.h>

#include "audio_util.h"

/* Helper: build a minimal WAV. */
static void build_test_wav(uint8_t *buf, size_t data_bytes)
{
    uint32_t file_size = data_bytes + 36;
    uint32_t sr = 16000;
    uint16_t ch = 1, bits = 16;
    uint32_t byte_rate = sr * ch * bits / 8;
    uint16_t block_align = ch * bits / 8;
    uint32_t fmt_size = 16;
    uint16_t fmt_pcm = 1;

    memcpy(buf, "RIFF", 4);      memcpy(buf + 4, &file_size, 4);
    memcpy(buf + 8, "WAVEfmt ", 8);
    memcpy(buf + 16, &fmt_size, 4); memcpy(buf + 20, &fmt_pcm, 2);
    memcpy(buf + 22, &ch, 2);    memcpy(buf + 24, &sr, 4);
    memcpy(buf + 28, &byte_rate, 4); memcpy(buf + 32, &block_align, 2);
    memcpy(buf + 34, &bits, 2);  memcpy(buf + 36, "data", 4);
    memcpy(buf + 40, &data_bytes, 4);
}

TEST_CASE("audio_is_wav detects RIFF header", "[audio]")
{
    uint8_t wav[44];
    build_test_wav(wav, 0);
    TEST_ASSERT_TRUE(audio_is_wav(wav, sizeof(wav)));
}

TEST_CASE("audio_is_wav rejects non-WAV", "[audio]")
{
    uint8_t garbage[44];
    memset(garbage, 0xAA, sizeof(garbage));
    TEST_ASSERT_FALSE(audio_is_wav(garbage, sizeof(garbage)));
}

TEST_CASE("audio_is_wav rejects short buffer", "[audio]")
{
    uint8_t buf[4] = {'R', 'I', 'F', 'F'};
    TEST_ASSERT_FALSE(audio_is_wav(buf, 4));
}

TEST_CASE("audio_wav_extract_pcm finds data", "[audio]")
{
    int16_t pcm[100];
    for (int i = 0; i < 100; i++) pcm[i] = (int16_t)(i * 100);

    uint8_t wav[44 + 200];
    build_test_wav(wav, 200);
    memcpy(wav + 44, pcm, 200);

    const int16_t *out_pcm;
    size_t out_samples;
    uint32_t sr;
    uint16_t ch;
    TEST_ASSERT_TRUE(audio_wav_extract_pcm(wav, sizeof(wav), &out_pcm, &out_samples, &sr, &ch));
    TEST_ASSERT_EQUAL_UINT(100, out_samples);
    TEST_ASSERT_EQUAL_UINT32(16000, sr);
    TEST_ASSERT_EQUAL_UINT16(1, ch);
    TEST_ASSERT_EQUAL_INT16(pcm[0], out_pcm[0]);
    TEST_ASSERT_EQUAL_INT16(pcm[99], out_pcm[99]);
}

TEST_CASE("audio_wav_extract_pcm rejects non-WAV", "[audio]")
{
    uint8_t garbage[100];
    memset(garbage, 0, sizeof(garbage));
    const int16_t *p;
    size_t n;
    TEST_ASSERT_FALSE(audio_wav_extract_pcm(garbage, sizeof(garbage), &p, &n, NULL, NULL));
}

TEST_CASE("audio_apply_volume full volume is identity", "[audio]")
{
    int16_t pcm[4] = {1000, -1000, 32767, -32768};
    int16_t orig[4];
    memcpy(orig, pcm, sizeof(pcm));
    audio_apply_volume(pcm, 4, 100);
    TEST_ASSERT_EQUAL_MEMORY(orig, pcm, sizeof(pcm));
}

TEST_CASE("audio_apply_volume zero volume is silence", "[audio]")
{
    int16_t pcm[4] = {1000, -1000, 32767, -32768};
    audio_apply_volume(pcm, 4, 0);
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL_INT16(0, pcm[i]);
    }
}

TEST_CASE("audio_apply_volume 50 percent halves amplitude", "[audio]")
{
    int16_t pcm[2] = {1000, -1000};
    audio_apply_volume(pcm, 2, 50);
    /* 1000 * 128/256 = 500 (fixed-point: 50*256/100=128, 1000*128>>8=500) */
    TEST_ASSERT_INT16_WITHIN(5, 500, pcm[0]);
    TEST_ASSERT_INT16_WITHIN(5, -500, pcm[1]);
}

TEST_CASE("audio_apply_volume small volume", "[audio]")
{
    int16_t pcm[1] = {10000};
    audio_apply_volume(pcm, 1, 10);
    /* 10000 * 25/256 ≈ 976 (10*256/100=25, 10000*25>>8=976) */
    TEST_ASSERT_INT16_WITHIN(50, 976, pcm[0]);
}
