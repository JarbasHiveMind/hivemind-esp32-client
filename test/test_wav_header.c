/**
 * @file test_wav_header.c
 * @brief Unit tests for WAV header building and parsing (used by ovos_http and main.c).
 *
 * Tests the WAV header logic extracted from ovos_http.c.
 * We test the header structure directly since it's critical for STT/TTS HTTP.
 */
#include "unity.h"
#include <string.h>
#include <stdint.h>

/* Build WAV header — duplicated from ovos_http.c for standalone testing. */
static void build_wav_header(uint8_t *hdr, uint32_t data_bytes)
{
    uint32_t file_size = data_bytes + 36;
    uint32_t sr = 16000;
    uint16_t ch = 1, bits = 16;
    uint32_t byte_rate = sr * ch * bits / 8;
    uint16_t block_align = ch * bits / 8;
    uint32_t fmt_size = 16;
    uint16_t fmt_pcm = 1;

    memcpy(hdr, "RIFF", 4);      memcpy(hdr + 4, &file_size, 4);
    memcpy(hdr + 8, "WAVEfmt ", 8);
    memcpy(hdr + 16, &fmt_size, 4); memcpy(hdr + 20, &fmt_pcm, 2);
    memcpy(hdr + 22, &ch, 2);    memcpy(hdr + 24, &sr, 4);
    memcpy(hdr + 28, &byte_rate, 4); memcpy(hdr + 32, &block_align, 2);
    memcpy(hdr + 34, &bits, 2);  memcpy(hdr + 36, "data", 4);
    memcpy(hdr + 40, &data_bytes, 4);
}

/* Parse WAV — duplicated from ovos_http.c. */
static int wav_find_data(const uint8_t *wav, size_t wav_len,
                          size_t *data_offset, size_t *data_size)
{
    if (wav_len < 44) return -1;
    if (memcmp(wav, "RIFF", 4) != 0 || memcmp(wav + 8, "WAVE", 4) != 0) return -1;
    size_t pos = 12;
    while (pos + 8 <= wav_len) {
        uint32_t chunk_size;
        memcpy(&chunk_size, wav + pos + 4, 4);
        if (memcmp(wav + pos, "data", 4) == 0) {
            *data_offset = pos + 8;
            *data_size = chunk_size;
            return 0;
        }
        pos += 8 + chunk_size;
        if (pos % 2 != 0) pos++;
    }
    return -1;
}

TEST_CASE("wav header has correct RIFF marker", "[wav]")
{
    uint8_t hdr[44];
    build_wav_header(hdr, 960);
    TEST_ASSERT_EQUAL_MEMORY("RIFF", hdr, 4);
    TEST_ASSERT_EQUAL_MEMORY("WAVE", hdr + 8, 4);
}

TEST_CASE("wav header has correct file size", "[wav]")
{
    uint8_t hdr[44];
    uint32_t data_bytes = 960;
    build_wav_header(hdr, data_bytes);
    uint32_t file_size;
    memcpy(&file_size, hdr + 4, 4);
    TEST_ASSERT_EQUAL_UINT32(data_bytes + 36, file_size);
}

TEST_CASE("wav header has correct sample rate", "[wav]")
{
    uint8_t hdr[44];
    build_wav_header(hdr, 960);
    uint32_t sr;
    memcpy(&sr, hdr + 24, 4);
    TEST_ASSERT_EQUAL_UINT32(16000, sr);
}

TEST_CASE("wav header has correct format", "[wav]")
{
    uint8_t hdr[44];
    build_wav_header(hdr, 960);
    uint16_t fmt, ch, bits;
    memcpy(&fmt, hdr + 20, 2);
    memcpy(&ch, hdr + 22, 2);
    memcpy(&bits, hdr + 34, 2);
    TEST_ASSERT_EQUAL_UINT16(1, fmt);    /* PCM */
    TEST_ASSERT_EQUAL_UINT16(1, ch);     /* Mono */
    TEST_ASSERT_EQUAL_UINT16(16, bits);  /* 16-bit */
}

TEST_CASE("wav find_data locates data chunk", "[wav]")
{
    uint8_t wav[44 + 960];
    build_wav_header(wav, 960);
    memset(wav + 44, 0, 960);

    size_t offset, size;
    int ret = wav_find_data(wav, sizeof(wav), &offset, &size);
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_UINT(44, offset);
    TEST_ASSERT_EQUAL_UINT(960, size);
}

TEST_CASE("wav find_data rejects non-WAV", "[wav]")
{
    uint8_t garbage[44];
    memset(garbage, 0xAA, sizeof(garbage));

    size_t offset, size;
    int ret = wav_find_data(garbage, sizeof(garbage), &offset, &size);
    TEST_ASSERT_NOT_EQUAL(0, ret);
}

TEST_CASE("wav find_data rejects too-short buffer", "[wav]")
{
    uint8_t short_buf[10] = {0};
    size_t offset, size;
    int ret = wav_find_data(short_buf, sizeof(short_buf), &offset, &size);
    TEST_ASSERT_NOT_EQUAL(0, ret);
}

TEST_CASE("wav roundtrip build then parse", "[wav]")
{
    /* Build a WAV with 480 samples (960 bytes) of PCM data. */
    int16_t pcm[480];
    for (int i = 0; i < 480; i++) pcm[i] = (int16_t)(i * 10);

    uint8_t wav[44 + 960];
    build_wav_header(wav, 960);
    memcpy(wav + 44, pcm, 960);

    size_t offset, size;
    int ret = wav_find_data(wav, sizeof(wav), &offset, &size);
    TEST_ASSERT_EQUAL_INT(0, ret);

    /* Verify PCM data is intact. */
    int16_t *parsed_pcm = (int16_t *)(wav + offset);
    for (int i = 0; i < 480; i++) {
        TEST_ASSERT_EQUAL_INT16(pcm[i], parsed_pcm[i]);
    }
}
