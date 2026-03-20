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
    TEST_ASSERT_EQUAL_STRING("PONG", hm_msg_type_str(HM_MSG_PONG));
}

TEST_CASE("msg_type_parse roundtrip string to enum to string", "[binary]")
{
    const char *names[] = {
        "HANDSHAKE", "BUS", "SHARED_BUS", "BROADCAST", "PROPAGATE",
        "ESCALATE", "HELLO", "QUERY", "CASCADE", "PING",
        "RENDEZVOUS", "THIRDPARTY", "BINARY", "PONG"
    };

    for (int i = 0; i < 14; i++) {
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
