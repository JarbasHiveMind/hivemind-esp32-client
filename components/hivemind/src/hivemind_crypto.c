/**
 * @file hivemind_crypto.c
 * @brief HiveMind crypto layer using mbedTLS (ESP-IDF built-in).
 */

#include "hivemind_crypto.h"

#include <string.h>
#include <stdio.h>
#include <mbedtls/sha256.h>
#include <mbedtls/pkcs5.h>
#include <mbedtls/gcm.h>
#include <mbedtls/chachapoly.h>
#include <esp_random.h>
#include <esp_log.h>

static const char *TAG = "hm_crypto";

/* --------------- Hex helpers --------------- */

static void hex_encode(const uint8_t *src, size_t len, char *dst)
{
    for (size_t i = 0; i < len; i++) {
        sprintf(dst + i * 2, "%02x", src[i]);
    }
}

static int hex_decode(const char *src, size_t hex_len, uint8_t *dst)
{
    if (hex_len % 2 != 0) {
        return -1;
    }
    for (size_t i = 0; i < hex_len / 2; i++) {
        unsigned int val;
        if (sscanf(src + i * 2, "%2x", &val) != 1) {
            return -1;
        }
        dst[i] = (uint8_t)val;
    }
    return 0;
}

/* --------------- JSON helpers --------------- */

/** Extract hex value for a given key from JSON like {"key":"hexval",...} */
static int json_extract_hex(const char *json, const char *key,
                            uint8_t *out, size_t max_out, size_t *out_len)
{
    char pattern[32];
    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    const char *start = strstr(json, pattern);
    if (!start) {
        return -1;
    }
    start += strlen(pattern);
    const char *end = strchr(start, '"');
    if (!end) {
        return -1;
    }
    size_t hex_len = (size_t)(end - start);
    if (hex_len / 2 > max_out) {
        return -1;
    }
    if (hex_decode(start, hex_len, out) != 0) {
        return -1;
    }
    *out_len = hex_len / 2;
    return 0;
}

/* --------------- Encrypt/decrypt core --------------- */

static esp_err_t encrypt_core(const hm_crypto_ctx_t *ctx,
                              const uint8_t *pt, size_t pt_len,
                              const uint8_t *nonce, size_t nonce_len,
                              uint8_t *ct, uint8_t tag[HM_AUTH_TAG_SIZE])
{
    int ret;
    if (ctx->cipher == HM_CIPHER_AES_GCM) {
        mbedtls_gcm_context gcm;
        mbedtls_gcm_init(&gcm);
        ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, ctx->key, 256);
        if (ret == 0) {
            ret = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT,
                                            pt_len, nonce, nonce_len,
                                            NULL, 0, pt, ct,
                                            HM_AUTH_TAG_SIZE, tag);
        }
        mbedtls_gcm_free(&gcm);
    } else {
        mbedtls_chachapoly_context cp;
        mbedtls_chachapoly_init(&cp);
        ret = mbedtls_chachapoly_setkey(&cp, ctx->key);
        if (ret == 0) {
            ret = mbedtls_chachapoly_encrypt_and_tag(&cp, pt_len,
                                                     nonce, NULL, 0,
                                                     pt, ct, tag);
        }
        mbedtls_chachapoly_free(&cp);
    }
    if (ret != 0) {
        ESP_LOGE(TAG, "Encryption failed: -0x%04x", (unsigned)-ret);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t decrypt_core(const hm_crypto_ctx_t *ctx,
                              const uint8_t *ct, size_t ct_len,
                              const uint8_t *nonce, size_t nonce_len,
                              const uint8_t tag[HM_AUTH_TAG_SIZE],
                              uint8_t *pt)
{
    int ret;
    if (ctx->cipher == HM_CIPHER_AES_GCM) {
        mbedtls_gcm_context gcm;
        mbedtls_gcm_init(&gcm);
        ret = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, ctx->key, 256);
        if (ret == 0) {
            ret = mbedtls_gcm_auth_decrypt(&gcm, ct_len, nonce, nonce_len,
                                           NULL, 0, tag, HM_AUTH_TAG_SIZE,
                                           ct, pt);
        }
        mbedtls_gcm_free(&gcm);
    } else {
        mbedtls_chachapoly_context cp;
        mbedtls_chachapoly_init(&cp);
        ret = mbedtls_chachapoly_setkey(&cp, ctx->key);
        if (ret == 0) {
            ret = mbedtls_chachapoly_auth_decrypt(&cp, ct_len, nonce,
                                                  NULL, 0, tag,
                                                  ct, pt);
        }
        mbedtls_chachapoly_free(&cp);
    }
    if (ret != 0) {
        ESP_LOGE(TAG, "Decryption failed: -0x%04x", (unsigned)-ret);
        return (ret == MBEDTLS_ERR_GCM_AUTH_FAILED ||
                ret == MBEDTLS_ERR_CHACHAPOLY_AUTH_FAILED)
                   ? ESP_ERR_INVALID_STATE
                   : ESP_FAIL;
    }
    return ESP_OK;
}

/* --------------- Public API --------------- */

esp_err_t hm_crypto_generate_hsub(const char *password, uint8_t iv_out[HM_HSUB_IV_SIZE],
                                   char hsub_out[HM_HSUB_HEX_LEN + 1])
{
    /* Generate 8 random bytes as IV */
    esp_fill_random(iv_out, HM_HSUB_IV_SIZE);

    /* SHA256(IV + password_bytes) */
    size_t pw_len = strlen(password);
    uint8_t hash[32];
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);
    mbedtls_sha256_update(&sha, iv_out, HM_HSUB_IV_SIZE);
    mbedtls_sha256_update(&sha, (const uint8_t *)password, pw_len);
    mbedtls_sha256_finish(&sha, hash);
    mbedtls_sha256_free(&sha);

    /* Concatenate IV (8B) + hash (32B) = 40 bytes, hex-encode first 24 bytes = 48 hex chars */
    uint8_t combined[40];
    memcpy(combined, iv_out, HM_HSUB_IV_SIZE);
    memcpy(combined + HM_HSUB_IV_SIZE, hash, 32);

    hex_encode(combined, 24, hsub_out);
    hsub_out[HM_HSUB_HEX_LEN] = '\0';

    return ESP_OK;
}

esp_err_t hm_crypto_extract_iv(const char *hsub_hex, uint8_t iv_out[HM_HSUB_IV_SIZE])
{
    if (strlen(hsub_hex) < HM_HSUB_HEX_LEN) {
        ESP_LOGE(TAG, "hSub too short");
        return ESP_ERR_INVALID_ARG;
    }
    /* First 16 hex chars = 8 bytes IV */
    if (hex_decode(hsub_hex, 16, iv_out) != 0) {
        ESP_LOGE(TAG, "Invalid hex in hSub IV");
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

esp_err_t hm_crypto_derive_key(const char *password,
                                const uint8_t client_iv[HM_HSUB_IV_SIZE],
                                const uint8_t server_iv[HM_HSUB_IV_SIZE],
                                hm_crypto_ctx_t *ctx_out)
{
    /* XOR IVs to form salt */
    uint8_t salt[HM_HSUB_IV_SIZE];
    for (int i = 0; i < HM_HSUB_IV_SIZE; i++) {
        salt[i] = client_iv[i] ^ server_iv[i];
    }

    int ret = mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256,
                                             (const uint8_t *)password, strlen(password),
                                             salt, HM_HSUB_IV_SIZE,
                                             HM_PBKDF2_ITERATIONS,
                                             HM_SESSION_KEY_SIZE, ctx_out->key);
    if (ret != 0) {
        ESP_LOGE(TAG, "PBKDF2 failed: -0x%04x", (unsigned)-ret);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t hm_crypto_encrypt_json_hex(const hm_crypto_ctx_t *ctx,
                                      const uint8_t *plaintext, size_t pt_len,
                                      char *json_out, size_t json_out_sz)
{
    size_t nonce_len = hm_crypto_nonce_size(ctx->cipher);
    uint8_t nonce[HM_AES_GCM_NONCE_SIZE];
    esp_fill_random(nonce, nonce_len);

    uint8_t *ct = (uint8_t *)malloc(pt_len);
    if (!ct) {
        return ESP_ERR_NO_MEM;
    }
    uint8_t tag[HM_AUTH_TAG_SIZE];

    esp_err_t err = encrypt_core(ctx, plaintext, pt_len, nonce, nonce_len, ct, tag);
    if (err != ESP_OK) {
        free(ct);
        return err;
    }

    /* Hex-encode all fields */
    char *ct_hex = (char *)malloc(pt_len * 2 + 1);
    char tag_hex[HM_AUTH_TAG_SIZE * 2 + 1];
    char nonce_hex[HM_AES_GCM_NONCE_SIZE * 2 + 1];
    if (!ct_hex) {
        free(ct);
        return ESP_ERR_NO_MEM;
    }

    hex_encode(ct, pt_len, ct_hex);
    ct_hex[pt_len * 2] = '\0';
    hex_encode(tag, HM_AUTH_TAG_SIZE, tag_hex);
    tag_hex[HM_AUTH_TAG_SIZE * 2] = '\0';
    hex_encode(nonce, nonce_len, nonce_hex);
    nonce_hex[nonce_len * 2] = '\0';

    int written = snprintf(json_out, json_out_sz,
                           "{\"ciphertext\":\"%s\",\"tag\":\"%s\",\"nonce\":\"%s\"}",
                           ct_hex, tag_hex, nonce_hex);

    free(ct_hex);
    free(ct);

    if (written < 0 || (size_t)written >= json_out_sz) {
        ESP_LOGE(TAG, "JSON output buffer too small");
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

esp_err_t hm_crypto_decrypt_json_hex(const hm_crypto_ctx_t *ctx,
                                      const char *json_hex,
                                      uint8_t *pt_out, size_t pt_out_sz,
                                      size_t *pt_len_out)
{
    size_t nonce_len = hm_crypto_nonce_size(ctx->cipher);

    /* Parse nonce */
    uint8_t nonce[HM_AES_GCM_NONCE_SIZE];
    size_t parsed_nonce_len;
    if (json_extract_hex(json_hex, "nonce", nonce, sizeof(nonce), &parsed_nonce_len) != 0 ||
        parsed_nonce_len != nonce_len) {
        ESP_LOGE(TAG, "Failed to parse nonce");
        return ESP_ERR_INVALID_ARG;
    }

    /* Parse tag */
    uint8_t tag[HM_AUTH_TAG_SIZE];
    size_t parsed_tag_len;
    if (json_extract_hex(json_hex, "tag", tag, sizeof(tag), &parsed_tag_len) != 0 ||
        parsed_tag_len != HM_AUTH_TAG_SIZE) {
        ESP_LOGE(TAG, "Failed to parse tag");
        return ESP_ERR_INVALID_ARG;
    }

    /* Parse ciphertext */
    /* Find ciphertext hex length first */
    const char *ct_start = strstr(json_hex, "\"ciphertext\":\"");
    if (!ct_start) {
        return ESP_ERR_INVALID_ARG;
    }
    ct_start += strlen("\"ciphertext\":\"");
    const char *ct_end = strchr(ct_start, '"');
    if (!ct_end) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t ct_hex_len = (size_t)(ct_end - ct_start);
    size_t ct_len = ct_hex_len / 2;

    if (ct_len > pt_out_sz) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t *ct = (uint8_t *)malloc(ct_len);
    if (!ct) {
        return ESP_ERR_NO_MEM;
    }
    if (hex_decode(ct_start, ct_hex_len, ct) != 0) {
        free(ct);
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = decrypt_core(ctx, ct, ct_len, nonce, nonce_len, tag, pt_out);
    free(ct);
    if (err == ESP_OK) {
        *pt_len_out = ct_len;
    }
    return err;
}

esp_err_t hm_crypto_encrypt_binary(const hm_crypto_ctx_t *ctx,
                                    const uint8_t *plaintext, size_t pt_len,
                                    uint8_t *out, size_t out_sz, size_t *out_len)
{
    size_t nonce_len = hm_crypto_nonce_size(ctx->cipher);
    size_t total = nonce_len + pt_len + HM_AUTH_TAG_SIZE;
    if (total > out_sz) {
        return ESP_ERR_INVALID_SIZE;
    }

    /* Generate random nonce at start of output */
    esp_fill_random(out, nonce_len);

    uint8_t *ct = out + nonce_len;
    uint8_t *tag = out + nonce_len + pt_len;

    esp_err_t err = encrypt_core(ctx, plaintext, pt_len, out, nonce_len, ct, tag);
    if (err == ESP_OK) {
        *out_len = total;
    }
    return err;
}

esp_err_t hm_crypto_decrypt_binary(const hm_crypto_ctx_t *ctx,
                                    const uint8_t *frame, size_t frame_len,
                                    uint8_t *pt_out, size_t pt_out_sz,
                                    size_t *pt_len_out)
{
    size_t nonce_len = hm_crypto_nonce_size(ctx->cipher);
    if (frame_len < nonce_len + HM_AUTH_TAG_SIZE) {
        ESP_LOGE(TAG, "Frame too short");
        return ESP_ERR_INVALID_SIZE;
    }

    size_t ct_len = frame_len - nonce_len - HM_AUTH_TAG_SIZE;
    if (ct_len > pt_out_sz) {
        return ESP_ERR_INVALID_SIZE;
    }

    const uint8_t *nonce = frame;
    const uint8_t *ct = frame + nonce_len;
    const uint8_t *tag = frame + nonce_len + ct_len;

    esp_err_t err = decrypt_core(ctx, ct, ct_len, nonce, nonce_len, tag, pt_out);
    if (err == ESP_OK) {
        *pt_len_out = ct_len;
    }
    return err;
}

esp_err_t hm_crypto_parse_cipher(const char *name, hm_cipher_t *cipher_out)
{
    if (strcasecmp(name, "AES-GCM") == 0) {
        *cipher_out = HM_CIPHER_AES_GCM;
        return ESP_OK;
    }
    if (strcasecmp(name, "CHACHA20-POLY1305") == 0) {
        *cipher_out = HM_CIPHER_CHACHA20_POLY1305;
        return ESP_OK;
    }
    ESP_LOGE(TAG, "Unknown cipher: %s", name);
    return ESP_ERR_NOT_FOUND;
}
