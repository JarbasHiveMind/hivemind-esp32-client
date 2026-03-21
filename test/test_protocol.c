/**
 * @file test_protocol.c
 * @brief Unity tests for hivemind_protocol.h handshake FSM and envelope functions.
 */
#include "unity.h"
#include <string.h>
#include "hivemind_protocol.h"
#include "cJSON.h"

/* ================================================================
 * Initialization tests
 * ================================================================ */

TEST_CASE("protocol init sets CONNECTING state", "[protocol]")
{
    hm_protocol_ctx_t ctx;
    hm_protocol_init(&ctx, "password", "living_room", HM_CIPHER_AES_GCM);

    TEST_ASSERT_EQUAL(HM_STATE_CONNECTING, ctx.state);
}

TEST_CASE("protocol init stores password and site_id", "[protocol]")
{
    hm_protocol_ctx_t ctx;
    hm_protocol_init(&ctx, "secret123", "kitchen", HM_CIPHER_AES_GCM);

    TEST_ASSERT_EQUAL_STRING("secret123", ctx.password);
    TEST_ASSERT_EQUAL_STRING("kitchen", ctx.site_id);
}

TEST_CASE("protocol init generates 36-char session_id", "[protocol]")
{
    hm_protocol_ctx_t ctx;
    hm_protocol_init(&ctx, "pw", "site", HM_CIPHER_AES_GCM);

    TEST_ASSERT_EQUAL(36, strlen(ctx.session_id));
    /* UUID v4 format: 8-4-4-4-12 */
    TEST_ASSERT_EQUAL('-', ctx.session_id[8]);
    TEST_ASSERT_EQUAL('-', ctx.session_id[13]);
    TEST_ASSERT_EQUAL('-', ctx.session_id[18]);
    TEST_ASSERT_EQUAL('-', ctx.session_id[23]);
}

TEST_CASE("protocol init sets preferred cipher and encoding", "[protocol]")
{
    hm_protocol_ctx_t ctx;
    hm_protocol_init(&ctx, "pw", "site", HM_CIPHER_CHACHA20_POLY1305);

    TEST_ASSERT_EQUAL(HM_CIPHER_CHACHA20_POLY1305, ctx.preferred_cipher);
    TEST_ASSERT_EQUAL(HM_ENCODING_JSON_HEX, ctx.preferred_encoding);
    TEST_ASSERT_FALSE(ctx.binarize);
}

/* ================================================================
 * Envelope building tests
 * ================================================================ */

TEST_CASE("build envelope contains msg_type and payload", "[protocol]")
{
    cJSON *payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "key", "value");

    char buf[1024];
    TEST_ASSERT_EQUAL(ESP_OK, hm_protocol_build_envelope("bus", payload, buf, sizeof(buf)));

    cJSON *parsed = cJSON_Parse(buf);
    TEST_ASSERT_NOT_NULL(parsed);

    const cJSON *mt = cJSON_GetObjectItemCaseSensitive(parsed, "msg_type");
    TEST_ASSERT_NOT_NULL(mt);
    TEST_ASSERT_EQUAL_STRING("bus", mt->valuestring);

    const cJSON *pl = cJSON_GetObjectItemCaseSensitive(parsed, "payload");
    TEST_ASSERT_NOT_NULL(pl);
    const cJSON *k = cJSON_GetObjectItemCaseSensitive(pl, "key");
    TEST_ASSERT_NOT_NULL(k);
    TEST_ASSERT_EQUAL_STRING("value", k->valuestring);

    /* Verify standard envelope fields exist */
    TEST_ASSERT_NOT_NULL(cJSON_GetObjectItemCaseSensitive(parsed, "metadata"));
    TEST_ASSERT_NOT_NULL(cJSON_GetObjectItemCaseSensitive(parsed, "route"));
    TEST_ASSERT_NOT_NULL(cJSON_GetObjectItemCaseSensitive(parsed, "node"));

    cJSON_Delete(parsed);
    cJSON_Delete(payload);
}

TEST_CASE("build envelope with null payload creates empty object", "[protocol]")
{
    char buf[1024];
    TEST_ASSERT_EQUAL(ESP_OK, hm_protocol_build_envelope("hello", NULL, buf, sizeof(buf)));

    cJSON *parsed = cJSON_Parse(buf);
    TEST_ASSERT_NOT_NULL(parsed);

    const cJSON *pl = cJSON_GetObjectItemCaseSensitive(parsed, "payload");
    TEST_ASSERT_NOT_NULL(pl);
    TEST_ASSERT_TRUE(cJSON_IsObject(pl));

    cJSON_Delete(parsed);
}

TEST_CASE("build envelope returns INVALID_SIZE on buffer overflow", "[protocol]")
{
    cJSON *payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "data", "test");

    char tiny[10];
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE,
                      hm_protocol_build_envelope("bus", payload, tiny, sizeof(tiny)));

    cJSON_Delete(payload);
}

/* ================================================================
 * Handshake FSM tests
 * ================================================================ */

TEST_CASE("hello message transitions to HELLO_RECEIVED", "[protocol]")
{
    hm_protocol_ctx_t ctx;
    hm_protocol_init(&ctx, "pw", "site", HM_CIPHER_AES_GCM);

    const char *hello = "{\"msg_type\":\"hello\",\"payload\":"
                        "{\"pubkey\":\"abc\",\"peer\":\"server1\",\"node_id\":\"n1\"}}";
    char *reply = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, hm_protocol_handle_message(&ctx, hello, &reply));
    TEST_ASSERT_EQUAL(HM_STATE_HELLO_RECEIVED, ctx.state);
    TEST_ASSERT_EQUAL_STRING("abc", ctx.server_pubkey);
    TEST_ASSERT_EQUAL_STRING("server1", ctx.server_peer);
    TEST_ASSERT_EQUAL_STRING("n1", ctx.server_node_id);
    TEST_ASSERT_NULL(reply);
}

TEST_CASE("shake request in HELLO_RECEIVED sends envelope reply", "[protocol]")
{
    hm_protocol_ctx_t ctx;
    hm_protocol_init(&ctx, "testpass", "site", HM_CIPHER_AES_GCM);
    ctx.state = HM_STATE_HELLO_RECEIVED;

    const char *shake = "{\"msg_type\":\"handshake\",\"payload\":{\"handshake\":true}}";
    char *reply = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, hm_protocol_handle_message(&ctx, shake, &reply));
    TEST_ASSERT_EQUAL(HM_STATE_HANDSHAKE_SENT, ctx.state);
    TEST_ASSERT_NOT_NULL(reply);

    /* Reply should contain envelope field */
    cJSON *parsed = cJSON_Parse(reply);
    TEST_ASSERT_NOT_NULL(parsed);
    const cJSON *pl = cJSON_GetObjectItemCaseSensitive(parsed, "payload");
    TEST_ASSERT_NOT_NULL(pl);
    const cJSON *env = cJSON_GetObjectItemCaseSensitive(pl, "envelope");
    TEST_ASSERT_NOT_NULL(env);
    TEST_ASSERT_TRUE(cJSON_IsString(env));

    cJSON_Delete(parsed);
    free(reply);
}

TEST_CASE("shake response derives key and reaches READY", "[protocol]")
{
    hm_protocol_ctx_t ctx;
    hm_protocol_init(&ctx, "testpass", "site", HM_CIPHER_AES_GCM);

    /* Step 1: hello */
    const char *hello = "{\"msg_type\":\"hello\",\"payload\":{\"pubkey\":\"\",\"peer\":\"s\",\"node_id\":\"n\"}}";
    char *reply = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, hm_protocol_handle_message(&ctx, hello, &reply));
    TEST_ASSERT_NULL(reply);

    /* Step 2: shake request */
    const char *shake1 = "{\"msg_type\":\"handshake\",\"payload\":{\"handshake\":true}}";
    TEST_ASSERT_EQUAL(ESP_OK, hm_protocol_handle_message(&ctx, shake1, &reply));
    TEST_ASSERT_NOT_NULL(reply);
    free(reply);
    reply = NULL;

    /* Step 3: shake response with server hsub — generate a valid one */
    uint8_t server_iv[HM_HSUB_IV_SIZE];
    char server_hsub[HM_HSUB_HEX_LEN + 1];
    TEST_ASSERT_EQUAL(ESP_OK, hm_crypto_generate_hsub("testpass", server_iv, server_hsub));

    char shake2[512];
    snprintf(shake2, sizeof(shake2),
             "{\"msg_type\":\"handshake\",\"payload\":{\"envelope\":\"%s\"}}", server_hsub);

    TEST_ASSERT_EQUAL(ESP_OK, hm_protocol_handle_message(&ctx, shake2, &reply));
    TEST_ASSERT_EQUAL(HM_STATE_READY, ctx.state);
    TEST_ASSERT_NOT_NULL(reply);  /* encrypted hello */
    free(reply);
}

TEST_CASE("invalid JSON returns error", "[protocol]")
{
    hm_protocol_ctx_t ctx;
    hm_protocol_init(&ctx, "pw", "site", HM_CIPHER_AES_GCM);

    char *reply = NULL;
    TEST_ASSERT_NOT_EQUAL(ESP_OK, hm_protocol_handle_message(&ctx, "not json{{{", &reply));
    TEST_ASSERT_NULL(reply);
    TEST_ASSERT_EQUAL(HM_STATE_CONNECTING, ctx.state);
}

TEST_CASE("wrong message type in CONNECTING returns error", "[protocol]")
{
    hm_protocol_ctx_t ctx;
    hm_protocol_init(&ctx, "pw", "site", HM_CIPHER_AES_GCM);

    const char *shake = "{\"msg_type\":\"handshake\",\"payload\":{\"handshake\":true}}";
    char *reply = NULL;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      hm_protocol_handle_message(&ctx, shake, &reply));
    TEST_ASSERT_EQUAL(HM_STATE_CONNECTING, ctx.state);
}

/* ================================================================
 * Encrypt/decrypt tests
 * ================================================================ */

TEST_CASE("encrypt not-ready returns INVALID_STATE", "[protocol]")
{
    hm_protocol_ctx_t ctx;
    hm_protocol_init(&ctx, "pw", "site", HM_CIPHER_AES_GCM);

    cJSON *payload = cJSON_CreateObject();
    char buf[1024];
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      hm_protocol_encrypt_message(&ctx, "bus", payload, buf, sizeof(buf)));
    cJSON_Delete(payload);
}

TEST_CASE("decrypt not-ready returns INVALID_STATE", "[protocol]")
{
    hm_protocol_ctx_t ctx;
    hm_protocol_init(&ctx, "pw", "site", HM_CIPHER_AES_GCM);

    hm_msg_type_t type;
    cJSON *payload_out = NULL;
    cJSON *context_out = NULL;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      hm_protocol_decrypt_message(&ctx, "dummy", &type, &payload_out, &context_out));
}

TEST_CASE("encrypt then decrypt roundtrip preserves payload", "[protocol]")
{
    /* Set up a READY context by running full handshake */
    hm_protocol_ctx_t ctx;
    hm_protocol_init(&ctx, "roundtrip", "site", HM_CIPHER_AES_GCM);

    /* hello */
    const char *hello = "{\"msg_type\":\"hello\",\"payload\":{\"pubkey\":\"\",\"peer\":\"s\",\"node_id\":\"n\"}}";
    char *reply = NULL;
    hm_protocol_handle_message(&ctx, hello, &reply);

    /* shake request */
    const char *shake1 = "{\"msg_type\":\"handshake\",\"payload\":{\"handshake\":true}}";
    hm_protocol_handle_message(&ctx, shake1, &reply);
    free(reply);
    reply = NULL;

    /* shake response */
    uint8_t server_iv[HM_HSUB_IV_SIZE];
    char server_hsub[HM_HSUB_HEX_LEN + 1];
    hm_crypto_generate_hsub("roundtrip", server_iv, server_hsub);

    char shake2[512];
    snprintf(shake2, sizeof(shake2),
             "{\"msg_type\":\"handshake\",\"payload\":{\"envelope\":\"%s\"}}", server_hsub);
    hm_protocol_handle_message(&ctx, shake2, &reply);
    free(reply);

    TEST_ASSERT_EQUAL(HM_STATE_READY, ctx.state);

    /* Now encrypt a message */
    cJSON *payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "type", "recognizer_loop:utterance");
    cJSON_AddStringToObject(payload, "utterance", "hello world");

    char encrypted[4096];
    TEST_ASSERT_EQUAL(ESP_OK,
                      hm_protocol_encrypt_message(&ctx, "bus", payload, encrypted, sizeof(encrypted)));

    /* Decrypt it */
    hm_msg_type_t type_out;
    cJSON *payload_out = NULL;
    cJSON *context_out = NULL;
    TEST_ASSERT_EQUAL(ESP_OK,
                      hm_protocol_decrypt_message(&ctx, encrypted, &type_out, &payload_out, &context_out));

    TEST_ASSERT_EQUAL(HM_MSG_BUS, type_out);
    TEST_ASSERT_NOT_NULL(payload_out);

    /* Verify payload fields survived roundtrip */
    const cJSON *utt = cJSON_GetObjectItemCaseSensitive(payload_out, "utterance");
    TEST_ASSERT_NOT_NULL(utt);
    TEST_ASSERT_EQUAL_STRING("hello world", utt->valuestring);

    cJSON_Delete(payload);
    cJSON_Delete(payload_out);
    if (context_out) cJSON_Delete(context_out);
}
