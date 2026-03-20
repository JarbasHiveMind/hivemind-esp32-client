/**
 * @file hivemind_crypto.c
 * @brief HiveMind crypto layer using mbedTLS (ESP-IDF built-in).
 *
 * Supports 7 JSON field encodings: HEX, B64, URL-safe B64, B32, Z85B, Z85P, B91.
 */

#include "hivemind_crypto.h"

#include <string.h>
#include <stdio.h>
#include <mbedtls/sha256.h>
#include <mbedtls/pkcs5.h>
#include <mbedtls/gcm.h>
#include <mbedtls/chachapoly.h>
#include <mbedtls/base64.h>
#include <esp_random.h>
#include <esp_log.h>

static const char *TAG = "hm_crypto";

/* ================================================================
 *  Encoding name table
 * ================================================================ */

static const char *s_encoding_names[] = {
    [HM_ENCODING_JSON_HEX]          = "JSON-HEX",
    [HM_ENCODING_JSON_B64]          = "JSON-B64",
    [HM_ENCODING_JSON_URLSAFE_B64]  = "JSON-URLSAFE-B64",
    [HM_ENCODING_JSON_B32]          = "JSON-B32",
    [HM_ENCODING_JSON_Z85B]         = "JSON-Z85B",
    [HM_ENCODING_JSON_Z85P]         = "JSON-Z85P",
    [HM_ENCODING_JSON_B91]          = "JSON-B91",
};

#define NUM_ENCODINGS (sizeof(s_encoding_names) / sizeof(s_encoding_names[0]))

const char *hm_encoding_name(hm_encoding_t encoding)
{
    if ((unsigned)encoding < NUM_ENCODINGS) {
        return s_encoding_names[encoding];
    }
    return "UNKNOWN";
}

esp_err_t hm_encoding_parse(const char *name, hm_encoding_t *encoding_out)
{
    for (unsigned i = 0; i < NUM_ENCODINGS; i++) {
        if (strcasecmp(name, s_encoding_names[i]) == 0) {
            *encoding_out = (hm_encoding_t)i;
            return ESP_OK;
        }
    }
    ESP_LOGE(TAG, "Unknown encoding: %s", name);
    return ESP_ERR_NOT_FOUND;
}

/* ================================================================
 *  Hex encode / decode
 * ================================================================ */

static int hex_encode(const uint8_t *src, size_t len, char *dst, size_t dst_sz, size_t *out_len)
{
    size_t need = len * 2 + 1;
    if (dst_sz < need) {
        return -1;
    }
    for (size_t i = 0; i < len; i++) {
        sprintf(dst + i * 2, "%02x", src[i]);
    }
    dst[len * 2] = '\0';
    *out_len = len * 2;
    return 0;
}

static int hex_decode(const char *src, size_t src_len, uint8_t *dst, size_t dst_sz, size_t *out_len)
{
    if (src_len % 2 != 0 || src_len / 2 > dst_sz) {
        return -1;
    }
    for (size_t i = 0; i < src_len / 2; i++) {
        unsigned int val;
        if (sscanf(src + i * 2, "%2x", &val) != 1) {
            return -1;
        }
        dst[i] = (uint8_t)val;
    }
    *out_len = src_len / 2;
    return 0;
}

/* ================================================================
 *  Base64 encode / decode (standard, via mbedTLS)
 * ================================================================ */

static int b64_encode(const uint8_t *src, size_t len, char *dst, size_t dst_sz, size_t *out_len)
{
    size_t olen = 0;
    int ret = mbedtls_base64_encode((unsigned char *)dst, dst_sz, &olen, src, len);
    if (ret != 0) {
        return -1;
    }
    *out_len = olen;
    return 0;
}

static int b64_decode(const char *src, size_t src_len, uint8_t *dst, size_t dst_sz, size_t *out_len)
{
    size_t olen = 0;
    int ret = mbedtls_base64_decode(dst, dst_sz, &olen, (const unsigned char *)src, src_len);
    if (ret != 0) {
        return -1;
    }
    *out_len = olen;
    return 0;
}

/* ================================================================
 *  URL-safe Base64 encode / decode
 * ================================================================ */

static int urlsafe_b64_encode(const uint8_t *src, size_t len, char *dst, size_t dst_sz, size_t *out_len)
{
    if (b64_encode(src, len, dst, dst_sz, out_len) != 0) {
        return -1;
    }
    /* Replace +→-, /→_, strip trailing = */
    size_t w = 0;
    for (size_t i = 0; i < *out_len; i++) {
        if (dst[i] == '+') {
            dst[w++] = '-';
        } else if (dst[i] == '/') {
            dst[w++] = '_';
        } else if (dst[i] == '=') {
            /* skip padding */
        } else {
            dst[w++] = dst[i];
        }
    }
    dst[w] = '\0';
    *out_len = w;
    return 0;
}

static int urlsafe_b64_decode(const char *src, size_t src_len, uint8_t *dst, size_t dst_sz, size_t *out_len)
{
    /* Restore standard B64: -→+, _→/, add padding */
    size_t padded_sz = src_len + 4;
    char *tmp = (char *)malloc(padded_sz);
    if (!tmp) {
        return -1;
    }
    size_t w = 0;
    for (size_t i = 0; i < src_len; i++) {
        if (src[i] == '-') {
            tmp[w++] = '+';
        } else if (src[i] == '_') {
            tmp[w++] = '/';
        } else {
            tmp[w++] = src[i];
        }
    }
    /* Add padding */
    while (w % 4 != 0) {
        tmp[w++] = '=';
    }
    tmp[w] = '\0';
    int ret = b64_decode(tmp, w, dst, dst_sz, out_len);
    free(tmp);
    return ret;
}

/* ================================================================
 *  Base32 encode / decode (RFC 4648)
 * ================================================================ */

static const char B32_ALPHA[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

static int b32_encode(const uint8_t *src, size_t len, char *dst, size_t dst_sz, size_t *out_len)
{
    size_t need = ((len + 4) / 5) * 8 + 1;
    if (dst_sz < need) {
        return -1;
    }
    size_t w = 0;
    size_t i = 0;
    while (i < len) {
        uint64_t buf = 0;
        int bits_left = 0;
        /* Accumulate up to 5 bytes (40 bits) */
        for (int j = 0; j < 5 && i < len; j++, i++) {
            buf = (buf << 8) | src[i];
            bits_left += 8;
        }
        /* Shift left so the top bits are at position 39 */
        buf <<= (40 - bits_left);
        /* Extract 5-bit groups */
        int groups = (bits_left + 4) / 5;
        for (int g = 7; g >= 0; g--) {
            if (g < 8 - groups) {
                dst[w++] = '=';
            } else {
                dst[w++] = B32_ALPHA[(buf >> (g * 5)) & 0x1F];
            }
        }
    }
    dst[w] = '\0';
    *out_len = w;
    return 0;
}

static int b32_char_val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a';
    if (c >= '2' && c <= '7') return c - '2' + 26;
    return -1;
}

static int b32_decode(const char *src, size_t src_len, uint8_t *dst, size_t dst_sz, size_t *out_len)
{
    /* Strip padding for length calculation */
    while (src_len > 0 && src[src_len - 1] == '=') {
        src_len--;
    }
    size_t max_bytes = src_len * 5 / 8;
    if (max_bytes > dst_sz) {
        return -1;
    }

    uint32_t buf = 0;
    int bits = 0;
    size_t w = 0;
    for (size_t i = 0; i < src_len; i++) {
        int val = b32_char_val(src[i]);
        if (val < 0) {
            return -1;
        }
        buf = (buf << 5) | (uint32_t)val;
        bits += 5;
        if (bits >= 8) {
            bits -= 8;
            dst[w++] = (uint8_t)(buf >> bits);
            buf &= (1u << bits) - 1;
        }
    }
    *out_len = w;
    return 0;
}

/* ================================================================
 *  Z85 encode / decode (ZeroMQ Z85, 4 bytes → 5 chars)
 * ================================================================ */

static const char Z85_ALPHA[] =
    "0123456789abcdefghijklmnopqrstuvwxyz"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    ".-:+=^!/*?&<>()[]{}@%$#";

static int z85_char_val(char c)
{
    const char *p = strchr(Z85_ALPHA, c);
    if (!p || c == '\0') {
        return -1;
    }
    return (int)(p - Z85_ALPHA);
}

/**
 * @brief Z85 encode with mandatory padding to multiple of 4.
 *
 * For Z85B: caller must ensure src_len is already a multiple of 4.
 * For Z85P: the last byte of the padded input encodes the pad count.
 */
static int z85_encode_padded(const uint8_t *src, size_t len, char *dst, size_t dst_sz, size_t *out_len)
{
    /* Pad to multiple of 4 */
    size_t pad = (4 - (len % 4)) % 4;
    size_t padded_len = len + pad;
    size_t need = (padded_len / 4) * 5 + 1;
    if (dst_sz < need) {
        return -1;
    }

    size_t w = 0;
    for (size_t i = 0; i < padded_len; i += 4) {
        uint32_t val = 0;
        for (int j = 0; j < 4; j++) {
            size_t idx = i + j;
            uint8_t byte = (idx < len) ? src[idx] : (uint8_t)pad;
            val = (val << 8) | byte;
        }
        for (int j = 4; j >= 0; j--) {
            dst[w + j] = Z85_ALPHA[val % 85];
            val /= 85;
        }
        w += 5;
    }
    dst[w] = '\0';
    *out_len = w;
    return 0;
}

static int z85_decode_padded(const char *src, size_t src_len, uint8_t *dst, size_t dst_sz,
                              size_t *out_len, int strip_padding)
{
    if (src_len % 5 != 0) {
        return -1;
    }
    size_t num_blocks = src_len / 5;
    size_t raw_len = num_blocks * 4;
    if (raw_len > dst_sz) {
        return -1;
    }

    size_t w = 0;
    for (size_t i = 0; i < src_len; i += 5) {
        uint32_t val = 0;
        for (int j = 0; j < 5; j++) {
            int cv = z85_char_val(src[i + j]);
            if (cv < 0) {
                return -1;
            }
            val = val * 85 + (uint32_t)cv;
        }
        dst[w++] = (uint8_t)(val >> 24);
        dst[w++] = (uint8_t)(val >> 16);
        dst[w++] = (uint8_t)(val >> 8);
        dst[w++] = (uint8_t)(val);
    }

    if (strip_padding && w > 0) {
        /* Last byte encodes pad count */
        uint8_t pad = dst[w - 1];
        if (pad > 0 && pad <= 3 && pad < w) {
            w -= pad;
        }
    }
    *out_len = w;
    return 0;
}

static int z85b_encode(const uint8_t *src, size_t len, char *dst, size_t dst_sz, size_t *out_len)
{
    /* Z85B requires input to be multiple of 4; pad with zeros if needed */
    size_t pad = (4 - (len % 4)) % 4;
    size_t padded_len = len + pad;
    size_t need = (padded_len / 4) * 5 + 1;
    if (dst_sz < need) {
        return -1;
    }

    size_t w = 0;
    for (size_t i = 0; i < padded_len; i += 4) {
        uint32_t val = 0;
        for (int j = 0; j < 4; j++) {
            size_t idx = i + j;
            uint8_t byte = (idx < len) ? src[idx] : 0;
            val = (val << 8) | byte;
        }
        for (int j = 4; j >= 0; j--) {
            dst[w + j] = Z85_ALPHA[val % 85];
            val /= 85;
        }
        w += 5;
    }
    dst[w] = '\0';
    *out_len = w;
    return 0;
}

static int z85b_decode(const char *src, size_t src_len, uint8_t *dst, size_t dst_sz, size_t *out_len)
{
    return z85_decode_padded(src, src_len, dst, dst_sz, out_len, 0);
}

static int z85p_encode(const uint8_t *src, size_t len, char *dst, size_t dst_sz, size_t *out_len)
{
    return z85_encode_padded(src, len, dst, dst_sz, out_len);
}

static int z85p_decode(const char *src, size_t src_len, uint8_t *dst, size_t dst_sz, size_t *out_len)
{
    return z85_decode_padded(src, src_len, dst, dst_sz, out_len, 1);
}

/* ================================================================
 *  Base91 encode / decode
 * ================================================================ */

static const char B91_ALPHA[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
    "0123456789!#$%&()*+,./:;<=>?@[]^_`{|}~\"";

static int b91_char_val(char c)
{
    const char *p = strchr(B91_ALPHA, c);
    if (!p || c == '\0') {
        return -1;
    }
    return (int)(p - B91_ALPHA);
}

static int b91_encode(const uint8_t *src, size_t len, char *dst, size_t dst_sz, size_t *out_len)
{
    /* Worst case: ~23% expansion */
    size_t max_out = (len * 16 / 13) + 3;
    if (dst_sz < max_out) {
        return -1;
    }

    uint32_t queue = 0;
    int nbits = 0;
    size_t w = 0;

    for (size_t i = 0; i < len; i++) {
        queue |= (uint32_t)src[i] << nbits;
        nbits += 8;
        if (nbits > 13) {
            uint32_t val = queue & 8191; /* 13 bits */
            if (val > 88) {
                queue >>= 13;
                nbits -= 13;
            } else {
                val = queue & 16383; /* 14 bits */
                queue >>= 14;
                nbits -= 14;
            }
            dst[w++] = B91_ALPHA[val % 91];
            dst[w++] = B91_ALPHA[val / 91];
        }
    }
    if (nbits > 0) {
        dst[w++] = B91_ALPHA[queue % 91];
        if (nbits > 7 || queue > 90) {
            dst[w++] = B91_ALPHA[queue / 91];
        }
    }
    dst[w] = '\0';
    *out_len = w;
    return 0;
}

static int b91_decode(const char *src, size_t src_len, uint8_t *dst, size_t dst_sz, size_t *out_len)
{
    uint32_t queue = 0;
    int nbits = 0;
    int val = -1;
    size_t w = 0;

    for (size_t i = 0; i < src_len; i++) {
        int cv = b91_char_val(src[i]);
        if (cv < 0) {
            return -1;
        }
        if (val == -1) {
            val = cv;
        } else {
            val += cv * 91;
            queue |= (uint32_t)val << nbits;
            nbits += (val & 8191) > 88 ? 13 : 14;
            val = -1;
            while (nbits >= 8) {
                if (w >= dst_sz) {
                    return -1;
                }
                dst[w++] = (uint8_t)(queue & 0xFF);
                queue >>= 8;
                nbits -= 8;
            }
        }
    }
    if (val != -1) {
        if (w >= dst_sz) {
            return -1;
        }
        dst[w++] = (uint8_t)((queue | ((uint32_t)val << nbits)) & 0xFF);
    }
    *out_len = w;
    return 0;
}

/* ================================================================
 *  Generic encode / decode dispatch
 * ================================================================ */

static int encode_field(hm_encoding_t enc, const uint8_t *src, size_t src_len,
                        char *dst, size_t dst_sz, size_t *out_len)
{
    switch (enc) {
    case HM_ENCODING_JSON_HEX:          return hex_encode(src, src_len, dst, dst_sz, out_len);
    case HM_ENCODING_JSON_B64:          return b64_encode(src, src_len, dst, dst_sz, out_len);
    case HM_ENCODING_JSON_URLSAFE_B64:  return urlsafe_b64_encode(src, src_len, dst, dst_sz, out_len);
    case HM_ENCODING_JSON_B32:          return b32_encode(src, src_len, dst, dst_sz, out_len);
    case HM_ENCODING_JSON_Z85B:         return z85b_encode(src, src_len, dst, dst_sz, out_len);
    case HM_ENCODING_JSON_Z85P:         return z85p_encode(src, src_len, dst, dst_sz, out_len);
    case HM_ENCODING_JSON_B91:          return b91_encode(src, src_len, dst, dst_sz, out_len);
    default:                             return -1;
    }
}

static int decode_field(hm_encoding_t enc, const char *src, size_t src_len,
                        uint8_t *dst, size_t dst_sz, size_t *out_len)
{
    switch (enc) {
    case HM_ENCODING_JSON_HEX:          return hex_decode(src, src_len, dst, dst_sz, out_len);
    case HM_ENCODING_JSON_B64:          return b64_decode(src, src_len, dst, dst_sz, out_len);
    case HM_ENCODING_JSON_URLSAFE_B64:  return urlsafe_b64_decode(src, src_len, dst, dst_sz, out_len);
    case HM_ENCODING_JSON_B32:          return b32_decode(src, src_len, dst, dst_sz, out_len);
    case HM_ENCODING_JSON_Z85B:         return z85b_decode(src, src_len, dst, dst_sz, out_len);
    case HM_ENCODING_JSON_Z85P:         return z85p_decode(src, src_len, dst, dst_sz, out_len);
    case HM_ENCODING_JSON_B91:          return b91_decode(src, src_len, dst, dst_sz, out_len);
    default:                             return -1;
    }
}

/* ================================================================
 *  JSON field extraction (encoding-generic)
 * ================================================================ */

/**
 * @brief Extract an encoded string value for a given key from JSON,
 *        then decode it to binary.
 */
static int json_extract_field(const char *json, const char *key,
                               hm_encoding_t encoding,
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
    size_t enc_len = (size_t)(end - start);
    return decode_field(encoding, start, enc_len, out, max_out, out_len);
}

/* ================================================================
 *  Encrypt/decrypt core (unchanged)
 * ================================================================ */

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

/* ================================================================
 *  Public API
 * ================================================================ */

esp_err_t hm_crypto_generate_hsub(const char *password, uint8_t iv_out[HM_HSUB_IV_SIZE],
                                   char hsub_out[HM_HSUB_HEX_LEN + 1])
{
    esp_fill_random(iv_out, HM_HSUB_IV_SIZE);

    size_t pw_len = strlen(password);
    uint8_t hash[32];
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);
    mbedtls_sha256_update(&sha, iv_out, HM_HSUB_IV_SIZE);
    mbedtls_sha256_update(&sha, (const uint8_t *)password, pw_len);
    mbedtls_sha256_finish(&sha, hash);
    mbedtls_sha256_free(&sha);

    uint8_t combined[40];
    memcpy(combined, iv_out, HM_HSUB_IV_SIZE);
    memcpy(combined + HM_HSUB_IV_SIZE, hash, 32);

    size_t olen;
    hex_encode(combined, 24, hsub_out, HM_HSUB_HEX_LEN + 1, &olen);
    hsub_out[HM_HSUB_HEX_LEN] = '\0';

    return ESP_OK;
}

esp_err_t hm_crypto_extract_iv(const char *hsub_hex, uint8_t iv_out[HM_HSUB_IV_SIZE])
{
    if (strlen(hsub_hex) < HM_HSUB_HEX_LEN) {
        ESP_LOGE(TAG, "hSub too short");
        return ESP_ERR_INVALID_ARG;
    }
    size_t olen;
    if (hex_decode(hsub_hex, 16, iv_out, HM_HSUB_IV_SIZE, &olen) != 0) {
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

esp_err_t hm_crypto_encrypt_json(const hm_crypto_ctx_t *ctx,
                                  const uint8_t *plaintext, size_t pt_len,
                                  hm_encoding_t encoding,
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

    /* Encode all fields — allocate generous buffers */
    size_t ct_enc_sz = pt_len * 3 + 16;
    char *ct_enc = (char *)malloc(ct_enc_sz);
    if (!ct_enc) {
        free(ct);
        return ESP_ERR_NO_MEM;
    }
    char tag_enc[128];
    char nonce_enc[128];
    size_t ct_enc_len, tag_enc_len, nonce_enc_len;

    int ok = encode_field(encoding, ct, pt_len, ct_enc, ct_enc_sz, &ct_enc_len);
    if (ok == 0) {
        ok = encode_field(encoding, tag, HM_AUTH_TAG_SIZE, tag_enc, sizeof(tag_enc), &tag_enc_len);
    }
    if (ok == 0) {
        ok = encode_field(encoding, nonce, nonce_len, nonce_enc, sizeof(nonce_enc), &nonce_enc_len);
    }

    free(ct);

    if (ok != 0) {
        free(ct_enc);
        ESP_LOGE(TAG, "Encoding failed for %s", hm_encoding_name(encoding));
        return ESP_FAIL;
    }

    int written = snprintf(json_out, json_out_sz,
                           "{\"ciphertext\":\"%s\",\"tag\":\"%s\",\"nonce\":\"%s\"}",
                           ct_enc, tag_enc, nonce_enc);

    free(ct_enc);

    if (written < 0 || (size_t)written >= json_out_sz) {
        ESP_LOGE(TAG, "JSON output buffer too small");
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

esp_err_t hm_crypto_decrypt_json(const hm_crypto_ctx_t *ctx,
                                  const char *json_str,
                                  hm_encoding_t encoding,
                                  uint8_t *pt_out, size_t pt_out_sz,
                                  size_t *pt_len_out)
{
    size_t nonce_len = hm_crypto_nonce_size(ctx->cipher);

    /* Parse nonce */
    uint8_t nonce[HM_AES_GCM_NONCE_SIZE];
    size_t parsed_nonce_len;
    if (json_extract_field(json_str, "nonce", encoding, nonce, sizeof(nonce), &parsed_nonce_len) != 0 ||
        parsed_nonce_len != nonce_len) {
        ESP_LOGE(TAG, "Failed to parse nonce");
        return ESP_ERR_INVALID_ARG;
    }

    /* Parse tag */
    uint8_t tag[HM_AUTH_TAG_SIZE];
    size_t parsed_tag_len;
    if (json_extract_field(json_str, "tag", encoding, tag, sizeof(tag), &parsed_tag_len) != 0 ||
        parsed_tag_len != HM_AUTH_TAG_SIZE) {
        ESP_LOGE(TAG, "Failed to parse tag");
        return ESP_ERR_INVALID_ARG;
    }

    /* Parse ciphertext — find encoded string length first */
    const char *ct_start = strstr(json_str, "\"ciphertext\":\"");
    if (!ct_start) {
        return ESP_ERR_INVALID_ARG;
    }
    ct_start += strlen("\"ciphertext\":\"");
    const char *ct_end = strchr(ct_start, '"');
    if (!ct_end) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t ct_enc_len = (size_t)(ct_end - ct_start);

    /* Decode ciphertext into temp buffer */
    size_t ct_max = ct_enc_len; /* decoded is always <= encoded length */
    uint8_t *ct = (uint8_t *)malloc(ct_max);
    if (!ct) {
        return ESP_ERR_NO_MEM;
    }
    size_t ct_len;
    if (decode_field(encoding, ct_start, ct_enc_len, ct, ct_max, &ct_len) != 0) {
        free(ct);
        ESP_LOGE(TAG, "Failed to decode ciphertext");
        return ESP_ERR_INVALID_ARG;
    }

    if (ct_len > pt_out_sz) {
        free(ct);
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t err = decrypt_core(ctx, ct, ct_len, nonce, nonce_len, tag, pt_out);
    free(ct);
    if (err == ESP_OK) {
        *pt_len_out = ct_len;
    }
    return err;
}

/* Legacy wrappers */

esp_err_t hm_crypto_encrypt_json_hex(const hm_crypto_ctx_t *ctx,
                                      const uint8_t *plaintext, size_t pt_len,
                                      char *json_out, size_t json_out_sz)
{
    return hm_crypto_encrypt_json(ctx, plaintext, pt_len, HM_ENCODING_JSON_HEX, json_out, json_out_sz);
}

esp_err_t hm_crypto_decrypt_json_hex(const hm_crypto_ctx_t *ctx,
                                      const char *json_hex,
                                      uint8_t *pt_out, size_t pt_out_sz,
                                      size_t *pt_len_out)
{
    return hm_crypto_decrypt_json(ctx, json_hex, HM_ENCODING_JSON_HEX, pt_out, pt_out_sz, pt_len_out);
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
