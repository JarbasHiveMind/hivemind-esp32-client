/**
 * @file test_noise.c
 * @brief Interop tests for the protocol-v3 Noise handshake (hivemind_noise.h)
 *        against fixtures generated from the Python reference implementation
 *        (poorman_handshake.noise / noiseprotocol) — see noise_fixtures.h.
 *
 * The C initiator must produce byte-identical handshake messages 1 and 3,
 * decrypt the responder's message 2 and transport frames, and produce
 * byte-identical initiator transport frames.
 */
#include "unity.h"
#include <string.h>
#include "hivemind_noise.h"
#include "hivemind_protocol.h"
#include "noise_fixtures.h"
#include "cJSON.h"

/* Run the XXpsk2 handshake up to completion against the fixtures. */
static void run_xx_handshake(hm_noise_ctx_t *ctx)
{
    TEST_ASSERT_EQUAL(ESP_OK, hm_noise_init(ctx, HM_NOISE_PATTERN_XXPSK2,
                                            fx_psk, fx_s_initiator_priv, NULL,
                                            fx_xx_prologue, FX_XX_PROLOGUE_LEN,
                                            fx_e_initiator_priv));

    uint8_t msg1[256];
    size_t msg1_len = 0;
    TEST_ASSERT_EQUAL(ESP_OK, hm_noise_write_message(ctx, fx_msg1_payload,
                                                     FX_MSG1_PAYLOAD_LEN,
                                                     msg1, sizeof(msg1), &msg1_len));
    TEST_ASSERT_EQUAL(FX_XX_MSG1_LEN, msg1_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(fx_xx_msg1, msg1, FX_XX_MSG1_LEN);

    uint8_t payload[128];
    size_t payload_len = 0;
    TEST_ASSERT_EQUAL(ESP_OK, hm_noise_read_message(ctx, fx_xx_msg2, FX_XX_MSG2_LEN,
                                                    payload, sizeof(payload),
                                                    &payload_len));
    TEST_ASSERT_EQUAL(FX_MSG2_PAYLOAD_LEN, payload_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(fx_msg2_payload, payload, FX_MSG2_PAYLOAD_LEN);
    TEST_ASSERT_FALSE(ctx->finished);

    uint8_t msg3[128];
    size_t msg3_len = 0;
    TEST_ASSERT_EQUAL(ESP_OK, hm_noise_write_message(ctx, NULL, 0,
                                                     msg3, sizeof(msg3), &msg3_len));
    TEST_ASSERT_EQUAL(FX_XX_MSG3_LEN, msg3_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(fx_xx_msg3, msg3, FX_XX_MSG3_LEN);
    TEST_ASSERT_TRUE(ctx->finished);
}

TEST_CASE("XXpsk2 handshake is byte-identical to the Python reference", "[noise]")
{
    hm_noise_ctx_t ctx;
    run_xx_handshake(&ctx);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(fx_xx_handshake_hash, ctx.handshake_hash, 32);
    /* the server static key was learned during the handshake (for pinning) */
    TEST_ASSERT_TRUE(ctx.has_rs);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(fx_s_responder_pub, ctx.rs, 32);
}

TEST_CASE("XXpsk2 transport frames interop with the Python reference", "[noise]")
{
    hm_noise_ctx_t ctx;
    run_xx_handshake(&ctx);

    /* initiator -> responder: byte-identical ciphertexts (counters 0, 1) */
    uint8_t ct[256];
    size_t ct_len = 0;
    TEST_ASSERT_EQUAL(ESP_OK, hm_noise_encrypt(&ctx, fx_transport_c2s,
                                               FX_TRANSPORT_C2S_LEN,
                                               ct, sizeof(ct), &ct_len));
    TEST_ASSERT_EQUAL(FX_XX_CT_C2S_LEN, ct_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(fx_xx_ct_c2s, ct, FX_XX_CT_C2S_LEN);

    TEST_ASSERT_EQUAL(ESP_OK, hm_noise_encrypt(&ctx, fx_transport_c2s_2,
                                               FX_TRANSPORT_C2S_2_LEN,
                                               ct, sizeof(ct), &ct_len));
    TEST_ASSERT_EQUAL(FX_XX_CT_C2S_2_LEN, ct_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(fx_xx_ct_c2s_2, ct, FX_XX_CT_C2S_2_LEN);

    /* responder -> initiator: decrypt reference ciphertexts in order */
    uint8_t pt[256];
    size_t pt_len = 0;
    TEST_ASSERT_EQUAL(ESP_OK, hm_noise_decrypt(&ctx, fx_xx_ct_s2c,
                                               FX_XX_CT_S2C_LEN,
                                               pt, sizeof(pt), &pt_len));
    TEST_ASSERT_EQUAL(FX_TRANSPORT_S2C_LEN, pt_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(fx_transport_s2c, pt, FX_TRANSPORT_S2C_LEN);

    TEST_ASSERT_EQUAL(ESP_OK, hm_noise_decrypt(&ctx, fx_xx_ct_s2c_bin,
                                               FX_XX_CT_S2C_BIN_LEN,
                                               pt, sizeof(pt), &pt_len));
    TEST_ASSERT_EQUAL(FX_TRANSPORT_S2C_BIN_LEN, pt_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(fx_transport_s2c_bin, pt, FX_TRANSPORT_S2C_BIN_LEN);
}

TEST_CASE("XXpsk2 replayed transport message is rejected", "[noise]")
{
    hm_noise_ctx_t ctx;
    run_xx_handshake(&ctx);

    uint8_t pt[256];
    size_t pt_len = 0;
    TEST_ASSERT_EQUAL(ESP_OK, hm_noise_decrypt(&ctx, fx_xx_ct_s2c,
                                               FX_XX_CT_S2C_LEN,
                                               pt, sizeof(pt), &pt_len));
    /* replay: same ciphertext again at counter 1 must fail AEAD auth */
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      hm_noise_decrypt(&ctx, fx_xx_ct_s2c, FX_XX_CT_S2C_LEN,
                                       pt, sizeof(pt), &pt_len));
    /* the failed counter was not consumed: the next in-order message works */
    TEST_ASSERT_EQUAL(ESP_OK, hm_noise_decrypt(&ctx, fx_xx_ct_s2c_bin,
                                               FX_XX_CT_S2C_BIN_LEN,
                                               pt, sizeof(pt), &pt_len));
}

TEST_CASE("XXpsk2 wrong PSK fails at handshake time", "[noise]")
{
    hm_noise_ctx_t ctx;
    uint8_t bad_psk[32];
    memcpy(bad_psk, fx_psk, 32);
    bad_psk[0] ^= 0x01;

    TEST_ASSERT_EQUAL(ESP_OK, hm_noise_init(&ctx, HM_NOISE_PATTERN_XXPSK2,
                                            bad_psk, fx_s_initiator_priv, NULL,
                                            fx_xx_prologue, FX_XX_PROLOGUE_LEN,
                                            fx_e_initiator_priv));
    uint8_t msg1[256];
    size_t msg1_len = 0;
    TEST_ASSERT_EQUAL(ESP_OK, hm_noise_write_message(&ctx, fx_msg1_payload,
                                                     FX_MSG1_PAYLOAD_LEN,
                                                     msg1, sizeof(msg1), &msg1_len));
    /* XXpsk2 mixes the PSK in message 2: the reference responder's message 2
     * must fail cryptographically, before any application data flows */
    uint8_t payload[128];
    size_t payload_len = 0;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      hm_noise_read_message(&ctx, fx_xx_msg2, FX_XX_MSG2_LEN,
                                            payload, sizeof(payload), &payload_len));
}

TEST_CASE("XXpsk2 tampered prologue fails at handshake time", "[noise]")
{
    hm_noise_ctx_t ctx;
    uint8_t bad_prologue[FX_XX_PROLOGUE_LEN];
    memcpy(bad_prologue, fx_xx_prologue, FX_XX_PROLOGUE_LEN);
    bad_prologue[10] ^= 0x01; /* e.g. a stripped suite / edited version bound */

    TEST_ASSERT_EQUAL(ESP_OK, hm_noise_init(&ctx, HM_NOISE_PATTERN_XXPSK2,
                                            fx_psk, fx_s_initiator_priv, NULL,
                                            bad_prologue, FX_XX_PROLOGUE_LEN,
                                            fx_e_initiator_priv));
    uint8_t msg1[256];
    size_t msg1_len = 0;
    TEST_ASSERT_EQUAL(ESP_OK, hm_noise_write_message(&ctx, fx_msg1_payload,
                                                     FX_MSG1_PAYLOAD_LEN,
                                                     msg1, sizeof(msg1), &msg1_len));
    uint8_t payload[128];
    size_t payload_len = 0;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      hm_noise_read_message(&ctx, fx_xx_msg2, FX_XX_MSG2_LEN,
                                            payload, sizeof(payload), &payload_len));
}

TEST_CASE("KKpsk0 handshake is byte-identical to the Python reference", "[noise]")
{
    hm_noise_ctx_t ctx;
    TEST_ASSERT_EQUAL(ESP_OK, hm_noise_init(&ctx, HM_NOISE_PATTERN_KKPSK0,
                                            fx_psk, fx_s_initiator_priv,
                                            fx_s_responder_pub,
                                            fx_kk_prologue, FX_KK_PROLOGUE_LEN,
                                            fx_e_initiator_priv));

    uint8_t msg1[256];
    size_t msg1_len = 0;
    TEST_ASSERT_EQUAL(ESP_OK, hm_noise_write_message(&ctx, fx_msg1_payload,
                                                     FX_MSG1_PAYLOAD_LEN,
                                                     msg1, sizeof(msg1), &msg1_len));
    TEST_ASSERT_EQUAL(FX_KK_MSG1_LEN, msg1_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(fx_kk_msg1, msg1, FX_KK_MSG1_LEN);

    /* message 2 completes KKpsk0 — no message 3 */
    uint8_t payload[128];
    size_t payload_len = 0;
    TEST_ASSERT_EQUAL(ESP_OK, hm_noise_read_message(&ctx, fx_kk_msg2, FX_KK_MSG2_LEN,
                                                    payload, sizeof(payload),
                                                    &payload_len));
    TEST_ASSERT_EQUAL(FX_MSG2_PAYLOAD_LEN, payload_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(fx_msg2_payload, payload, FX_MSG2_PAYLOAD_LEN);
    TEST_ASSERT_TRUE(ctx.finished);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(fx_kk_handshake_hash, ctx.handshake_hash, 32);

    /* transport interop, both directions */
    uint8_t ct[256];
    size_t ct_len = 0;
    TEST_ASSERT_EQUAL(ESP_OK, hm_noise_encrypt(&ctx, fx_transport_c2s,
                                               FX_TRANSPORT_C2S_LEN,
                                               ct, sizeof(ct), &ct_len));
    TEST_ASSERT_EQUAL(FX_KK_CT_C2S_LEN, ct_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(fx_kk_ct_c2s, ct, FX_KK_CT_C2S_LEN);

    uint8_t pt[256];
    size_t pt_len = 0;
    TEST_ASSERT_EQUAL(ESP_OK, hm_noise_decrypt(&ctx, fx_kk_ct_s2c,
                                               FX_KK_CT_S2C_LEN,
                                               pt, sizeof(pt), &pt_len));
    TEST_ASSERT_EQUAL(FX_TRANSPORT_S2C_LEN, pt_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(fx_transport_s2c, pt, FX_TRANSPORT_S2C_LEN);
}

TEST_CASE("KKpsk0 wrong PSK fails on message 2", "[noise]")
{
    hm_noise_ctx_t ctx;
    uint8_t bad_psk[32];
    memcpy(bad_psk, fx_psk, 32);
    bad_psk[31] ^= 0x80;

    TEST_ASSERT_EQUAL(ESP_OK, hm_noise_init(&ctx, HM_NOISE_PATTERN_KKPSK0,
                                            bad_psk, fx_s_initiator_priv,
                                            fx_s_responder_pub,
                                            fx_kk_prologue, FX_KK_PROLOGUE_LEN,
                                            fx_e_initiator_priv));
    uint8_t msg1[256];
    size_t msg1_len = 0;
    TEST_ASSERT_EQUAL(ESP_OK, hm_noise_write_message(&ctx, fx_msg1_payload,
                                                     FX_MSG1_PAYLOAD_LEN,
                                                     msg1, sizeof(msg1), &msg1_len));
    /* the PSK entered at psk0: message 1 already differs from the reference */
    TEST_ASSERT_TRUE(memcmp(fx_kk_msg1, msg1, FX_KK_MSG1_LEN) != 0);
    uint8_t payload[128];
    size_t payload_len = 0;
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE,
                      hm_noise_read_message(&ctx, fx_kk_msg2, FX_KK_MSG2_LEN,
                                            payload, sizeof(payload), &payload_len));
}

TEST_CASE("canonical JSON matches Python json.dumps sort_keys compact", "[noise]")
{
    /* mirror of HELLO_PAYLOAD in gen_noise_fixtures.py */
    cJSON *hello = cJSON_CreateObject();
    cJSON_AddStringToObject(hello, "pubkey",
        "-----BEGIN PUBLIC KEY-----\nAAAA\n-----END PUBLIC KEY-----");
    cJSON_AddStringToObject(hello, "peer", "tcp4:127.0.0.1:52250");
    cJSON_AddStringToObject(hello, "node_id", "hivemind-core@testhost");
    cJSON_AddStringToObject(hello, "session_id", "abcd1234");

    char *canon = hm_protocol_canonical_json(hello);
    cJSON_Delete(hello);
    TEST_ASSERT_NOT_NULL(canon);
    TEST_ASSERT_EQUAL(FX_CANONICAL_HELLO_LEN, strlen(canon));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(fx_canonical_hello, (uint8_t *)canon,
                                  FX_CANONICAL_HELLO_LEN);
    free(canon);

    /* mirror of HANDSHAKE_PAYLOAD (nested object, arrays, bools, ints) */
    cJSON *shake = cJSON_Parse(
        "{\"handshake\":true,\"binarize\":true,\"preshared_key\":false,"
        "\"password\":true,\"crypto_key\":false,\"min_protocol_version\":0,"
        "\"max_protocol_version\":3,"
        "\"encodings\":[\"JSON-HEX\",\"JSON-B64\"],"
        "\"ciphers\":[\"CHACHA20-POLY1305\",\"AES-GCM\"],"
        "\"noise\":{\"patterns\":[\"KKpsk0\",\"XXpsk2\"],"
        "\"suites\":[\"25519_ChaChaPoly_SHA256\"]}}");
    TEST_ASSERT_NOT_NULL(shake);
    canon = hm_protocol_canonical_json(shake);
    cJSON_Delete(shake);
    TEST_ASSERT_NOT_NULL(canon);
    TEST_ASSERT_EQUAL(FX_CANONICAL_HANDSHAKE_LEN, strlen(canon));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(fx_canonical_handshake, (uint8_t *)canon,
                                  FX_CANONICAL_HANDSHAKE_LEN);
    free(canon);
}

/* ================================================================
 * Protocol-level negotiation (HIVEMIND-WIRE-1 §2)
 * ================================================================ */

static const char *SERVER_HELLO_JSON =
    "{\"msg_type\":\"hello\",\"payload\":{"
    "\"pubkey\":\"-----BEGIN PUBLIC KEY-----\\nAAAA\\n-----END PUBLIC KEY-----\","
    "\"peer\":\"tcp4:127.0.0.1:52250\","
    "\"node_id\":\"hivemind-core@testhost\","
    "\"session_id\":\"abcd1234\"}}";

static const char *SERVER_SHAKE_V3_JSON =
    "{\"msg_type\":\"shake\",\"payload\":{"
    "\"handshake\":true,\"binarize\":true,\"preshared_key\":false,"
    "\"password\":true,\"crypto_key\":false,"
    "\"min_protocol_version\":0,\"max_protocol_version\":3,"
    "\"encodings\":[\"JSON-HEX\",\"JSON-B64\"],"
    "\"ciphers\":[\"CHACHA20-POLY1305\",\"AES-GCM\"],"
    "\"noise\":{\"patterns\":[\"KKpsk0\",\"XXpsk2\"],"
    "\"suites\":[\"25519_ChaChaPoly_SHA256\"]}}}";

TEST_CASE("v3 negotiation sends Noise message 1 in HANDSHAKE envelope", "[noise]")
{
    hm_protocol_ctx_t ctx;
    hm_protocol_init(&ctx, "password", "site", HM_CIPHER_CHACHA20_POLY1305);
    hm_protocol_set_v3(&ctx, fx_psk, fx_s_initiator_priv, NULL);

    char *reply = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, hm_protocol_handle_message(&ctx, SERVER_HELLO_JSON, &reply));
    TEST_ASSERT_NULL(reply);
    TEST_ASSERT_EQUAL(HM_STATE_HELLO_RECEIVED, ctx.state);
    /* HELLO payload retained canonically for the prologue */
    TEST_ASSERT_NOT_NULL(ctx.server_hello_canon);
    TEST_ASSERT_EQUAL(FX_CANONICAL_HELLO_LEN, strlen(ctx.server_hello_canon));
    TEST_ASSERT_EQUAL_MEMORY(fx_canonical_hello, ctx.server_hello_canon,
                             FX_CANONICAL_HELLO_LEN);

    TEST_ASSERT_EQUAL(ESP_OK, hm_protocol_handle_message(&ctx, SERVER_SHAKE_V3_JSON, &reply));
    TEST_ASSERT_NOT_NULL(reply);
    TEST_ASSERT_EQUAL(HM_STATE_NOISE_HANDSHAKE_SENT, ctx.state);

    /* No server static key pinned -> XXpsk2 selected */
    cJSON *env = cJSON_Parse(reply);
    TEST_ASSERT_NOT_NULL(env);
    cJSON *payload = cJSON_GetObjectItemCaseSensitive(env, "payload");
    cJSON *noise = cJSON_GetObjectItemCaseSensitive(payload, "noise");
    TEST_ASSERT_NOT_NULL(noise);
    TEST_ASSERT_EQUAL_STRING("XXpsk2",
        cJSON_GetObjectItemCaseSensitive(noise, "pattern")->valuestring);
    TEST_ASSERT_EQUAL_STRING("25519_ChaChaPoly_SHA256",
        cJSON_GetObjectItemCaseSensitive(noise, "suite")->valuestring);
    const char *msg_hex = cJSON_GetObjectItemCaseSensitive(noise, "msg")->valuestring;
    TEST_ASSERT_NOT_NULL(msg_hex);
    /* message 1 = 32-byte ephemeral + AEAD-protected payload */
    TEST_ASSERT_GREATER_THAN(2 * (32 + 16), strlen(msg_hex));
    cJSON_Delete(env);
    free(reply);
    hm_protocol_deinit(&ctx);
}

TEST_CASE("v3 negotiation prefers KKpsk0 when the server key is pinned", "[noise]")
{
    hm_protocol_ctx_t ctx;
    hm_protocol_init(&ctx, "password", "site", HM_CIPHER_CHACHA20_POLY1305);
    hm_protocol_set_v3(&ctx, fx_psk, fx_s_initiator_priv, fx_s_responder_pub);

    char *reply = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, hm_protocol_handle_message(&ctx, SERVER_HELLO_JSON, &reply));
    TEST_ASSERT_EQUAL(ESP_OK, hm_protocol_handle_message(&ctx, SERVER_SHAKE_V3_JSON, &reply));
    TEST_ASSERT_NOT_NULL(reply);
    TEST_ASSERT_EQUAL(HM_STATE_NOISE_HANDSHAKE_SENT, ctx.state);

    cJSON *env = cJSON_Parse(reply);
    cJSON *payload = cJSON_GetObjectItemCaseSensitive(env, "payload");
    cJSON *noise = cJSON_GetObjectItemCaseSensitive(payload, "noise");
    TEST_ASSERT_EQUAL_STRING("KKpsk0",
        cJSON_GetObjectItemCaseSensitive(noise, "pattern")->valuestring);
    cJSON_Delete(env);
    free(reply);
    hm_protocol_deinit(&ctx);
}

TEST_CASE("v2 server or missing PSK falls back to the legacy handshake", "[noise]")
{
    /* server without v3 -> legacy hsub even though a PSK is provisioned */
    hm_protocol_ctx_t ctx;
    hm_protocol_init(&ctx, "password", "site", HM_CIPHER_CHACHA20_POLY1305);
    hm_protocol_set_v3(&ctx, fx_psk, fx_s_initiator_priv, NULL);

    char *reply = NULL;
    TEST_ASSERT_EQUAL(ESP_OK, hm_protocol_handle_message(&ctx, SERVER_HELLO_JSON, &reply));
    const char *shake_v2 =
        "{\"msg_type\":\"shake\",\"payload\":{\"handshake\":true,"
        "\"max_protocol_version\":2,"
        "\"encodings\":[\"JSON-HEX\"],\"ciphers\":[\"AES-GCM\"]}}";
    TEST_ASSERT_EQUAL(ESP_OK, hm_protocol_handle_message(&ctx, shake_v2, &reply));
    TEST_ASSERT_NOT_NULL(reply);
    TEST_ASSERT_EQUAL(HM_STATE_HANDSHAKE_SENT, ctx.state);
    TEST_ASSERT_NOT_NULL(strstr(reply, "\"envelope\"")); /* legacy hsub */
    free(reply);
    reply = NULL;
    hm_protocol_deinit(&ctx);

    /* v3 server but no PSK provisioned -> legacy hsub */
    hm_protocol_init(&ctx, "password", "site", HM_CIPHER_CHACHA20_POLY1305);
    TEST_ASSERT_EQUAL(ESP_OK, hm_protocol_handle_message(&ctx, SERVER_HELLO_JSON, &reply));
    TEST_ASSERT_EQUAL(ESP_OK, hm_protocol_handle_message(&ctx, SERVER_SHAKE_V3_JSON, &reply));
    TEST_ASSERT_NOT_NULL(reply);
    TEST_ASSERT_EQUAL(HM_STATE_HANDSHAKE_SENT, ctx.state);
    TEST_ASSERT_NOT_NULL(strstr(reply, "\"envelope\""));
    free(reply);
    hm_protocol_deinit(&ctx);
}

TEST_CASE("noise transport frame markers roundtrip", "[noise]")
{
    /* two contexts spliced together: mirror one side's CipherStates */
    hm_protocol_ctx_t a;
    memset(&a, 0, sizeof(a));
    hm_noise_ctx_t na;
    run_xx_handshake(&na);
    a.noise = na;
    a.use_noise = true;

    const char *json = "{\"msg_type\":\"bus\"}";
    uint8_t frame[256];
    size_t frame_len = 0;
    TEST_ASSERT_EQUAL(ESP_OK, hm_protocol_noise_encrypt_frame(&a,
        (const uint8_t *)json, strlen(json), false,
        frame, sizeof(frame), &frame_len));
    TEST_ASSERT_EQUAL(strlen(json) + 1 + 16, frame_len);

    /* loop it back through a mirrored receiver (swap send/recv) */
    hm_protocol_ctx_t b;
    memset(&b, 0, sizeof(b));
    b.noise = na;
    b.noise.recv = a.noise.send;
    b.noise.recv.nonce = 0;
    b.use_noise = true;

    uint8_t pt[256];
    size_t pt_len = 0;
    bool is_binary = true;
    TEST_ASSERT_EQUAL(ESP_OK, hm_protocol_noise_decrypt_frame(&b,
        frame, frame_len, pt, sizeof(pt), &pt_len, &is_binary));
    TEST_ASSERT_FALSE(is_binary);
    TEST_ASSERT_EQUAL(strlen(json), pt_len);
    TEST_ASSERT_EQUAL_MEMORY(json, pt, pt_len);
}
