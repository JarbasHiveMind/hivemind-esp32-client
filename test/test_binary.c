/**
 * @file test_binary.c
 * @brief Unity tests for hivemind_binary.h encode/decode functions.
 */
#include "unity.h"
#include <string.h>
#include "hivemind_binary.h"

TEST_CASE("encode and decode BUS message with metadata and payload", "[binary]")
{
    const char *meta = "{\"session\":\"abc\"}";
    const char *payload = "recognizer_loop:utterance";
    uint8_t buf[512];
    size_t out_len = 0;

    TEST_ASSERT_EQUAL(ESP_OK, hm_binary_encode(
        HM_MSG_BUS, HM_BIN_UNDEFINED,
        (const uint8_t *)meta, strlen(meta),
        (const uint8_t *)payload, strlen(payload),
        buf, sizeof(buf), &out_len));

    TEST_ASSERT_GREATER_THAN(0, out_len);

    hm_binary_frame_t frame;
    TEST_ASSERT_EQUAL(ESP_OK, hm_binary_decode(buf, out_len, &frame));

    TEST_ASSERT_EQUAL(HM_MSG_BUS, frame.msg_type);
    TEST_ASSERT_EQUAL(strlen(meta), frame.metadata_len);
    TEST_ASSERT_EQUAL_MEMORY(meta, frame.metadata, frame.metadata_len);
    TEST_ASSERT_EQUAL(strlen(payload), frame.payload_len);
    TEST_ASSERT_EQUAL_MEMORY(payload, frame.payload, frame.payload_len);
}

TEST_CASE("encode and decode BINARY message with bin_type", "[binary]")
{
    uint8_t audio[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    uint8_t buf[256];
    size_t out_len = 0;

    TEST_ASSERT_EQUAL(ESP_OK, hm_binary_encode(
        HM_MSG_BINARY, HM_BIN_RAW_AUDIO,
        NULL, 0,
        audio, sizeof(audio),
        buf, sizeof(buf), &out_len));

    hm_binary_frame_t frame;
    TEST_ASSERT_EQUAL(ESP_OK, hm_binary_decode(buf, out_len, &frame));

    TEST_ASSERT_EQUAL(HM_MSG_BINARY, frame.msg_type);
    TEST_ASSERT_EQUAL(HM_BIN_RAW_AUDIO, frame.bin_type);
    TEST_ASSERT_EQUAL(sizeof(audio), frame.payload_len);
    TEST_ASSERT_EQUAL_MEMORY(audio, frame.payload, frame.payload_len);
}

TEST_CASE("msg_type_str returns correct strings", "[binary]")
{
    TEST_ASSERT_EQUAL_STRING("HANDSHAKE", hm_msg_type_str(HM_MSG_HANDSHAKE));
    TEST_ASSERT_EQUAL_STRING("BUS", hm_msg_type_str(HM_MSG_BUS));
    TEST_ASSERT_EQUAL_STRING("BINARY", hm_msg_type_str(HM_MSG_BINARY));
    TEST_ASSERT_EQUAL_STRING("PING", hm_msg_type_str(HM_MSG_PING));
}

TEST_CASE("msg_type_parse roundtrip string to enum to string", "[binary]")
{
    const char *names[] = {
        "HANDSHAKE", "BUS", "SHARED_BUS", "BROADCAST", "PROPAGATE",
        "ESCALATE", "HELLO", "QUERY", "CASCADE", "PING",
        "RENDEZVOUS", "THIRDPARTY", "BINARY"
    };

    for (int i = 0; i < 13; i++) {
        hm_msg_type_t t;
        TEST_ASSERT_EQUAL(ESP_OK, hm_msg_type_parse(names[i], &t));
        TEST_ASSERT_EQUAL_STRING(names[i], hm_msg_type_str(t));
    }
}

TEST_CASE("encode with empty metadata succeeds", "[binary]")
{
    const char *payload = "test";
    uint8_t buf[256];
    size_t out_len = 0;

    TEST_ASSERT_EQUAL(ESP_OK, hm_binary_encode(
        HM_MSG_BUS, HM_BIN_UNDEFINED,
        NULL, 0,
        (const uint8_t *)payload, strlen(payload),
        buf, sizeof(buf), &out_len));

    hm_binary_frame_t frame;
    TEST_ASSERT_EQUAL(ESP_OK, hm_binary_decode(buf, out_len, &frame));

    TEST_ASSERT_EQUAL(0, frame.metadata_len);
    TEST_ASSERT_EQUAL(strlen(payload), frame.payload_len);
}

TEST_CASE("encode and decode large 4KB payload", "[binary]")
{
    uint8_t payload[4096];
    for (int i = 0; i < 4096; i++) payload[i] = (uint8_t)(i & 0xFF);

    uint8_t buf[4608];
    size_t out_len = 0;

    TEST_ASSERT_EQUAL(ESP_OK, hm_binary_encode(
        HM_MSG_BINARY, HM_BIN_FILE,
        NULL, 0,
        payload, sizeof(payload),
        buf, sizeof(buf), &out_len));

    hm_binary_frame_t frame;
    TEST_ASSERT_EQUAL(ESP_OK, hm_binary_decode(buf, out_len, &frame));

    TEST_ASSERT_EQUAL(sizeof(payload), frame.payload_len);
    TEST_ASSERT_EQUAL_MEMORY(payload, frame.payload, sizeof(payload));
}

/* ── Cross-platform interop tests ─────────────────────────────────── */

TEST_CASE("known vector decode: unversioned BUS with metadata and payload", "[binary][interop]")
{
    /* Vector from vectors.json: BUS msg_type=1, metadata={}, payload={"type":"test"}
     * Unversioned encoding (versioned=false). */
    const uint8_t vector[] = {
        0x82, 0x02, 0x7b, 0x7d, 0x7b, 0x22, 0x74, 0x79,
        0x70, 0x65, 0x22, 0x3a, 0x22, 0x74, 0x65, 0x73,
        0x74, 0x22, 0x7d
    };

    hm_binary_frame_t frame;
    TEST_ASSERT_EQUAL(ESP_OK, hm_binary_decode(vector, sizeof(vector), &frame));

    TEST_ASSERT_EQUAL(HM_MSG_BUS, frame.msg_type);
    TEST_ASSERT_FALSE(frame.versioned);
    TEST_ASSERT_FALSE(frame.compressed);
    TEST_ASSERT_EQUAL(2, frame.metadata_len);
    TEST_ASSERT_EQUAL_MEMORY("{}", frame.metadata, 2);
    TEST_ASSERT_EQUAL(15, frame.payload_len);
    TEST_ASSERT_EQUAL_MEMORY("{\"type\":\"test\"}", frame.payload, 15);
}

TEST_CASE("roundtrip BUS message", "[binary][interop]")
{
    const char *meta = "{}";
    const char *payload = "{\"type\":\"test\"}";
    uint8_t buf[256];
    size_t out_len = 0;

    TEST_ASSERT_EQUAL(ESP_OK, hm_binary_encode(
        HM_MSG_BUS, HM_BIN_UNDEFINED,
        (const uint8_t *)meta, strlen(meta),
        (const uint8_t *)payload, strlen(payload),
        buf, sizeof(buf), &out_len));

    hm_binary_frame_t frame;
    TEST_ASSERT_EQUAL(ESP_OK, hm_binary_decode(buf, out_len, &frame));

    TEST_ASSERT_EQUAL(HM_MSG_BUS, frame.msg_type);
    TEST_ASSERT_TRUE(frame.versioned);
    TEST_ASSERT_EQUAL(1, frame.protocol_version);
    TEST_ASSERT_EQUAL_MEMORY(meta, frame.metadata, frame.metadata_len);
    TEST_ASSERT_EQUAL_MEMORY(payload, frame.payload, frame.payload_len);
}

TEST_CASE("roundtrip PROPAGATE message", "[binary][interop]")
{
    const char *meta = "{}";
    const char *payload = "{\"type\":\"propagate\"}";
    uint8_t buf[256];
    size_t out_len = 0;

    TEST_ASSERT_EQUAL(ESP_OK, hm_binary_encode(
        HM_MSG_PROPAGATE, HM_BIN_UNDEFINED,
        (const uint8_t *)meta, strlen(meta),
        (const uint8_t *)payload, strlen(payload),
        buf, sizeof(buf), &out_len));

    hm_binary_frame_t frame;
    TEST_ASSERT_EQUAL(ESP_OK, hm_binary_decode(buf, out_len, &frame));

    TEST_ASSERT_EQUAL(HM_MSG_PROPAGATE, frame.msg_type);
    TEST_ASSERT_EQUAL_MEMORY(payload, frame.payload, frame.payload_len);
}

TEST_CASE("roundtrip ESCALATE message", "[binary][interop]")
{
    const char *meta = "{}";
    const char *payload = "{\"type\":\"escalate\"}";
    uint8_t buf[256];
    size_t out_len = 0;

    TEST_ASSERT_EQUAL(ESP_OK, hm_binary_encode(
        HM_MSG_ESCALATE, HM_BIN_UNDEFINED,
        (const uint8_t *)meta, strlen(meta),
        (const uint8_t *)payload, strlen(payload),
        buf, sizeof(buf), &out_len));

    hm_binary_frame_t frame;
    TEST_ASSERT_EQUAL(ESP_OK, hm_binary_decode(buf, out_len, &frame));

    TEST_ASSERT_EQUAL(HM_MSG_ESCALATE, frame.msg_type);
    TEST_ASSERT_EQUAL_MEMORY(payload, frame.payload, frame.payload_len);
}

TEST_CASE("roundtrip PING message", "[binary][interop]")
{
    const char *meta = "{}";
    const char *payload = "{}";
    uint8_t buf[256];
    size_t out_len = 0;

    TEST_ASSERT_EQUAL(ESP_OK, hm_binary_encode(
        HM_MSG_PING, HM_BIN_UNDEFINED,
        (const uint8_t *)meta, strlen(meta),
        (const uint8_t *)payload, strlen(payload),
        buf, sizeof(buf), &out_len));

    hm_binary_frame_t frame;
    TEST_ASSERT_EQUAL(ESP_OK, hm_binary_decode(buf, out_len, &frame));

    TEST_ASSERT_EQUAL(HM_MSG_PING, frame.msg_type);
}

TEST_CASE("roundtrip BINARY with RAW_AUDIO bin_type", "[binary][interop]")
{
    uint8_t audio[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04};
    uint8_t buf[256];
    size_t out_len = 0;

    TEST_ASSERT_EQUAL(ESP_OK, hm_binary_encode(
        HM_MSG_BINARY, HM_BIN_RAW_AUDIO,
        NULL, 0,
        audio, sizeof(audio),
        buf, sizeof(buf), &out_len));

    hm_binary_frame_t frame;
    TEST_ASSERT_EQUAL(ESP_OK, hm_binary_decode(buf, out_len, &frame));

    TEST_ASSERT_EQUAL(HM_MSG_BINARY, frame.msg_type);
    TEST_ASSERT_EQUAL(HM_BIN_RAW_AUDIO, frame.bin_type);
    TEST_ASSERT_EQUAL(sizeof(audio), frame.payload_len);
    TEST_ASSERT_EQUAL_MEMORY(audio, frame.payload, sizeof(audio));
}

TEST_CASE("versioned encode-decode roundtrip", "[binary][interop]")
{
    const char *payload = "{\"type\":\"version_check\"}";
    uint8_t buf[256];
    size_t out_len = 0;

    TEST_ASSERT_EQUAL(ESP_OK, hm_binary_encode(
        HM_MSG_BUS, HM_BIN_UNDEFINED,
        (const uint8_t *)"{}", 2,
        (const uint8_t *)payload, strlen(payload),
        buf, sizeof(buf), &out_len));

    hm_binary_frame_t frame;
    TEST_ASSERT_EQUAL(ESP_OK, hm_binary_decode(buf, out_len, &frame));

    TEST_ASSERT_TRUE(frame.versioned);
    TEST_ASSERT_EQUAL(1, frame.protocol_version);
    TEST_ASSERT_EQUAL(HM_MSG_BUS, frame.msg_type);
    TEST_ASSERT_FALSE(frame.compressed);
}

TEST_CASE("large 4KB payload roundtrip interop", "[binary][interop]")
{
    uint8_t payload[4096];
    for (int i = 0; i < 4096; i++) payload[i] = (uint8_t)(i & 0xFF);

    const char *meta = "{\"large\":true}";
    uint8_t buf[4608];
    size_t out_len = 0;

    TEST_ASSERT_EQUAL(ESP_OK, hm_binary_encode(
        HM_MSG_BUS, HM_BIN_UNDEFINED,
        (const uint8_t *)meta, strlen(meta),
        payload, sizeof(payload),
        buf, sizeof(buf), &out_len));

    hm_binary_frame_t frame;
    TEST_ASSERT_EQUAL(ESP_OK, hm_binary_decode(buf, out_len, &frame));

    TEST_ASSERT_EQUAL(sizeof(payload), frame.payload_len);
    TEST_ASSERT_EQUAL_MEMORY(payload, frame.payload, sizeof(payload));
    TEST_ASSERT_EQUAL(strlen(meta), frame.metadata_len);
    TEST_ASSERT_EQUAL_MEMORY(meta, frame.metadata, frame.metadata_len);
}

TEST_CASE("all bin_types for BINARY: UNDEFINED", "[binary][interop]")
{
    uint8_t data[] = {0x01};
    uint8_t buf[64];
    size_t out_len = 0;

    TEST_ASSERT_EQUAL(ESP_OK, hm_binary_encode(
        HM_MSG_BINARY, HM_BIN_UNDEFINED, NULL, 0,
        data, sizeof(data), buf, sizeof(buf), &out_len));

    hm_binary_frame_t frame;
    TEST_ASSERT_EQUAL(ESP_OK, hm_binary_decode(buf, out_len, &frame));
    TEST_ASSERT_EQUAL(HM_BIN_UNDEFINED, frame.bin_type);
}

TEST_CASE("all bin_types for BINARY: NUMPY_IMAGE", "[binary][interop]")
{
    uint8_t data[] = {0x01};
    uint8_t buf[64];
    size_t out_len = 0;

    TEST_ASSERT_EQUAL(ESP_OK, hm_binary_encode(
        HM_MSG_BINARY, HM_BIN_NUMPY_IMAGE, NULL, 0,
        data, sizeof(data), buf, sizeof(buf), &out_len));

    hm_binary_frame_t frame;
    TEST_ASSERT_EQUAL(ESP_OK, hm_binary_decode(buf, out_len, &frame));
    TEST_ASSERT_EQUAL(HM_BIN_NUMPY_IMAGE, frame.bin_type);
}

TEST_CASE("all bin_types for BINARY: FILE", "[binary][interop]")
{
    uint8_t data[] = {0x01};
    uint8_t buf[64];
    size_t out_len = 0;

    TEST_ASSERT_EQUAL(ESP_OK, hm_binary_encode(
        HM_MSG_BINARY, HM_BIN_FILE, NULL, 0,
        data, sizeof(data), buf, sizeof(buf), &out_len));

    hm_binary_frame_t frame;
    TEST_ASSERT_EQUAL(ESP_OK, hm_binary_decode(buf, out_len, &frame));
    TEST_ASSERT_EQUAL(HM_BIN_FILE, frame.bin_type);
}

TEST_CASE("all bin_types for BINARY: STT_TRANSCRIBE", "[binary][interop]")
{
    uint8_t data[] = {0x01};
    uint8_t buf[64];
    size_t out_len = 0;

    TEST_ASSERT_EQUAL(ESP_OK, hm_binary_encode(
        HM_MSG_BINARY, HM_BIN_STT_TRANSCRIBE, NULL, 0,
        data, sizeof(data), buf, sizeof(buf), &out_len));

    hm_binary_frame_t frame;
    TEST_ASSERT_EQUAL(ESP_OK, hm_binary_decode(buf, out_len, &frame));
    TEST_ASSERT_EQUAL(HM_BIN_STT_TRANSCRIBE, frame.bin_type);
}

TEST_CASE("all bin_types for BINARY: STT_HANDLE", "[binary][interop]")
{
    uint8_t data[] = {0x01};
    uint8_t buf[64];
    size_t out_len = 0;

    TEST_ASSERT_EQUAL(ESP_OK, hm_binary_encode(
        HM_MSG_BINARY, HM_BIN_STT_HANDLE, NULL, 0,
        data, sizeof(data), buf, sizeof(buf), &out_len));

    hm_binary_frame_t frame;
    TEST_ASSERT_EQUAL(ESP_OK, hm_binary_decode(buf, out_len, &frame));
    TEST_ASSERT_EQUAL(HM_BIN_STT_HANDLE, frame.bin_type);
}

TEST_CASE("all bin_types for BINARY: TTS_AUDIO", "[binary][interop]")
{
    uint8_t data[] = {0x01};
    uint8_t buf[64];
    size_t out_len = 0;

    TEST_ASSERT_EQUAL(ESP_OK, hm_binary_encode(
        HM_MSG_BINARY, HM_BIN_TTS_AUDIO, NULL, 0,
        data, sizeof(data), buf, sizeof(buf), &out_len));

    hm_binary_frame_t frame;
    TEST_ASSERT_EQUAL(ESP_OK, hm_binary_decode(buf, out_len, &frame));
    TEST_ASSERT_EQUAL(HM_BIN_TTS_AUDIO, frame.bin_type);
}

TEST_CASE("metadata preservation with key-value JSON", "[binary][interop]")
{
    const char *meta = "{\"key\":\"value\"}";
    const char *payload = "test";
    uint8_t buf[256];
    size_t out_len = 0;

    TEST_ASSERT_EQUAL(ESP_OK, hm_binary_encode(
        HM_MSG_BUS, HM_BIN_UNDEFINED,
        (const uint8_t *)meta, strlen(meta),
        (const uint8_t *)payload, strlen(payload),
        buf, sizeof(buf), &out_len));

    hm_binary_frame_t frame;
    TEST_ASSERT_EQUAL(ESP_OK, hm_binary_decode(buf, out_len, &frame));

    TEST_ASSERT_EQUAL(strlen(meta), frame.metadata_len);
    TEST_ASSERT_EQUAL_MEMORY(meta, frame.metadata, frame.metadata_len);
    TEST_ASSERT_EQUAL(strlen(payload), frame.payload_len);
    TEST_ASSERT_EQUAL_MEMORY(payload, frame.payload, frame.payload_len);
}
