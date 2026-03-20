/**
 * @file test_crypto.c
 * @brief Unity tests for hivemind_crypto.h functions.
 */
#include "unity.h"
#include <string.h>
#include <ctype.h>
#include "hivemind_crypto.h"

static bool is_hex_string(const char *s, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        if (!isxdigit((unsigned char)s[i])) return false;
    }
    return true;
}

TEST_CASE("hsub generation produces 48 hex chars", "[crypto]")
{
    uint8_t iv[HM_HSUB_IV_SIZE];
    char hsub[HM_HSUB_HEX_LEN + 1];

    TEST_ASSERT_EQUAL(ESP_OK, hm_crypto_generate_hsub("testpass", iv, hsub));
    TEST_ASSERT_EQUAL(HM_HSUB_HEX_LEN, strlen(hsub));
    TEST_ASSERT_TRUE(is_hex_string(hsub, HM_HSUB_HEX_LEN));
}

TEST_CASE("hsub IV extraction matches original", "[crypto]")
{
    uint8_t iv_orig[HM_HSUB_IV_SIZE];
    uint8_t iv_extracted[HM_HSUB_IV_SIZE];
    char hsub[HM_HSUB_HEX_LEN + 1];

    TEST_ASSERT_EQUAL(ESP_OK, hm_crypto_generate_hsub("password123", iv_orig, hsub));
    TEST_ASSERT_EQUAL(ESP_OK, hm_crypto_extract_iv(hsub, iv_extracted));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(iv_orig, iv_extracted, HM_HSUB_IV_SIZE);
}

TEST_CASE("key derivation produces non-zero key", "[crypto]")
{
    uint8_t client_iv[HM_HSUB_IV_SIZE] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t server_iv[HM_HSUB_IV_SIZE] = {9, 10, 11, 12, 13, 14, 15, 16};
    hm_crypto_ctx_t ctx;

    TEST_ASSERT_EQUAL(ESP_OK, hm_crypto_derive_key("secret", client_iv, server_iv, &ctx));

    /* Key must not be all zeros */
    uint8_t zeros[HM_SESSION_KEY_SIZE] = {0};
    TEST_ASSERT_FALSE(memcmp(ctx.key, zeros, HM_SESSION_KEY_SIZE) == 0);
}

TEST_CASE("AES-GCM encrypt then decrypt roundtrip", "[crypto]")
{
    uint8_t client_iv[HM_HSUB_IV_SIZE] = {0xAA, 0xBB, 0xCC, 0xDD, 0x11, 0x22, 0x33, 0x44};
    uint8_t server_iv[HM_HSUB_IV_SIZE] = {0x55, 0x66, 0x77, 0x88, 0x99, 0x00, 0xEE, 0xFF};
    hm_crypto_ctx_t ctx;

    TEST_ASSERT_EQUAL(ESP_OK, hm_crypto_derive_key("aes-test", client_iv, server_iv, &ctx));
    ctx.cipher = HM_CIPHER_AES_GCM;

    const char *plaintext = "hello hivemind";
    size_t pt_len = strlen(plaintext);

    uint8_t enc[256];
    size_t enc_len = 0;
    TEST_ASSERT_EQUAL(ESP_OK, hm_crypto_encrypt_binary(&ctx,
        (const uint8_t *)plaintext, pt_len, enc, sizeof(enc), &enc_len));

    uint8_t dec[256];
    size_t dec_len = 0;
    TEST_ASSERT_EQUAL(ESP_OK, hm_crypto_decrypt_binary(&ctx,
        enc, enc_len, dec, sizeof(dec), &dec_len));

    TEST_ASSERT_EQUAL(pt_len, dec_len);
    TEST_ASSERT_EQUAL_MEMORY(plaintext, dec, pt_len);
}

TEST_CASE("ChaCha20-Poly1305 encrypt then decrypt roundtrip", "[crypto]")
{
    uint8_t client_iv[HM_HSUB_IV_SIZE] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t server_iv[HM_HSUB_IV_SIZE] = {8, 7, 6, 5, 4, 3, 2, 1};
    hm_crypto_ctx_t ctx;

    TEST_ASSERT_EQUAL(ESP_OK, hm_crypto_derive_key("chacha-test", client_iv, server_iv, &ctx));
    ctx.cipher = HM_CIPHER_CHACHA20_POLY1305;

    const char *plaintext = "chacha20 test data";
    size_t pt_len = strlen(plaintext);

    uint8_t enc[256];
    size_t enc_len = 0;
    TEST_ASSERT_EQUAL(ESP_OK, hm_crypto_encrypt_binary(&ctx,
        (const uint8_t *)plaintext, pt_len, enc, sizeof(enc), &enc_len));

    uint8_t dec[256];
    size_t dec_len = 0;
    TEST_ASSERT_EQUAL(ESP_OK, hm_crypto_decrypt_binary(&ctx,
        enc, enc_len, dec, sizeof(dec), &dec_len));

    TEST_ASSERT_EQUAL(pt_len, dec_len);
    TEST_ASSERT_EQUAL_MEMORY(plaintext, dec, pt_len);
}

TEST_CASE("JSON-HEX encrypt then decrypt roundtrip", "[crypto]")
{
    uint8_t client_iv[HM_HSUB_IV_SIZE] = {10, 20, 30, 40, 50, 60, 70, 80};
    uint8_t server_iv[HM_HSUB_IV_SIZE] = {80, 70, 60, 50, 40, 30, 20, 10};
    hm_crypto_ctx_t ctx;

    TEST_ASSERT_EQUAL(ESP_OK, hm_crypto_derive_key("json-test", client_iv, server_iv, &ctx));
    ctx.cipher = HM_CIPHER_AES_GCM;

    const char *plaintext = "{\"type\":\"test\",\"data\":{}}";
    size_t pt_len = strlen(plaintext);

    char json_enc[1024];
    TEST_ASSERT_EQUAL(ESP_OK, hm_crypto_encrypt_json_hex(&ctx,
        (const uint8_t *)plaintext, pt_len, json_enc, sizeof(json_enc)));

    uint8_t dec[512];
    size_t dec_len = 0;
    TEST_ASSERT_EQUAL(ESP_OK, hm_crypto_decrypt_json_hex(&ctx,
        json_enc, dec, sizeof(dec), &dec_len));

    TEST_ASSERT_EQUAL(pt_len, dec_len);
    TEST_ASSERT_EQUAL_MEMORY(plaintext, dec, pt_len);
}

TEST_CASE("binary encrypt then decrypt roundtrip", "[crypto]")
{
    uint8_t client_iv[HM_HSUB_IV_SIZE] = {0};
    uint8_t server_iv[HM_HSUB_IV_SIZE] = {0xFF, 0xFE, 0xFD, 0xFC, 0xFB, 0xFA, 0xF9, 0xF8};
    hm_crypto_ctx_t ctx;

    TEST_ASSERT_EQUAL(ESP_OK, hm_crypto_derive_key("bin-test", client_iv, server_iv, &ctx));
    ctx.cipher = HM_CIPHER_AES_GCM;

    uint8_t payload[64];
    for (int i = 0; i < 64; i++) payload[i] = (uint8_t)i;

    uint8_t enc[256];
    size_t enc_len = 0;
    TEST_ASSERT_EQUAL(ESP_OK, hm_crypto_encrypt_binary(&ctx,
        payload, sizeof(payload), enc, sizeof(enc), &enc_len));

    uint8_t dec[256];
    size_t dec_len = 0;
    TEST_ASSERT_EQUAL(ESP_OK, hm_crypto_decrypt_binary(&ctx,
        enc, enc_len, dec, sizeof(dec), &dec_len));

    TEST_ASSERT_EQUAL(sizeof(payload), dec_len);
    TEST_ASSERT_EQUAL_MEMORY(payload, dec, sizeof(payload));
}

TEST_CASE("cipher name parse roundtrip", "[crypto]")
{
    hm_cipher_t cipher;

    TEST_ASSERT_EQUAL(ESP_OK, hm_crypto_parse_cipher("AES-GCM", &cipher));
    TEST_ASSERT_EQUAL(HM_CIPHER_AES_GCM, cipher);

    TEST_ASSERT_EQUAL(ESP_OK, hm_crypto_parse_cipher("CHACHA20-POLY1305", &cipher));
    TEST_ASSERT_EQUAL(HM_CIPHER_CHACHA20_POLY1305, cipher);

    TEST_ASSERT_NOT_EQUAL(ESP_OK, hm_crypto_parse_cipher("UNKNOWN", &cipher));
}
