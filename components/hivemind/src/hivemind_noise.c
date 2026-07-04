/**
 * @file hivemind_noise.c
 * @brief Noise Protocol Framework (revision 34) initiator for HiveMind
 *        protocol v3 — XXpsk2 / KKpsk0 over 25519_ChaChaPoly_SHA256.
 *
 * All cryptography is mbedTLS: SHA-256 / HMAC-SHA256 for the hash and the
 * Noise HKDF, mbedtls_ecp on Curve25519 for X25519, and mbedtls_chachapoly
 * for the AEAD. The ChaChaPoly AEAD nonce is 4 zero bytes followed by the
 * 64-bit **little-endian** CipherState counter, per the Noise specification.
 */

#include "hivemind_noise.h"

#include <string.h>
#include <stdio.h>
#include <mbedtls/sha256.h>
#include <mbedtls/md.h>
#include <mbedtls/chachapoly.h>
#include <mbedtls/ecp.h>
#include <mbedtls/bignum.h>
#include <esp_random.h>
#include <esp_log.h>

static const char *TAG = "hm_noise";

/* Full Noise protocol names — both are > 32 bytes, so h = SHA-256(name). */
static const char *NOISE_NAME_XXPSK2 = "Noise_XXpsk2_25519_ChaChaPoly_SHA256";
static const char *NOISE_NAME_KKPSK0 = "Noise_KKpsk0_25519_ChaChaPoly_SHA256";

const char *hm_noise_pattern_name(hm_noise_pattern_t pattern)
{
    return (pattern == HM_NOISE_PATTERN_KKPSK0) ? "KKpsk0" : "XXpsk2";
}

/* ================================================================
 *  Hash / HKDF primitives (SHA-256)
 * ================================================================ */

static void sha256_2(const uint8_t *a, size_t alen,
                     const uint8_t *b, size_t blen,
                     uint8_t out[32])
{
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);
    if (alen) mbedtls_sha256_update(&sha, a, alen);
    if (blen) mbedtls_sha256_update(&sha, b, blen);
    mbedtls_sha256_finish(&sha, out);
    mbedtls_sha256_free(&sha);
}

static int hmac_sha256(const uint8_t key[32],
                       const uint8_t *d1, size_t l1,
                       const uint8_t *d2, size_t l2,
                       uint8_t out[32])
{
    const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    mbedtls_md_context_t c;
    mbedtls_md_init(&c);
    int ret = mbedtls_md_setup(&c, md, 1);
    if (ret == 0) ret = mbedtls_md_hmac_starts(&c, key, 32);
    if (ret == 0 && l1) ret = mbedtls_md_hmac_update(&c, d1, l1);
    if (ret == 0 && l2) ret = mbedtls_md_hmac_update(&c, d2, l2);
    if (ret == 0) ret = mbedtls_md_hmac_finish(&c, out);
    mbedtls_md_free(&c);
    return ret;
}

/** Noise HKDF: out1/out2/out3 = HKDF(chaining_key, ikm, num_outputs). */
static int noise_hkdf(const uint8_t ck[32], const uint8_t *ikm, size_t ikm_len,
                      uint8_t out1[32], uint8_t out2[32], uint8_t *out3 /* may be NULL */)
{
    uint8_t temp[32];
    uint8_t byte;
    int ret = hmac_sha256(ck, ikm, ikm_len, NULL, 0, temp);
    if (ret != 0) return ret;
    byte = 0x01;
    ret = hmac_sha256(temp, &byte, 1, NULL, 0, out1);
    if (ret != 0) return ret;
    byte = 0x02;
    ret = hmac_sha256(temp, out1, 32, &byte, 1, out2);
    if (ret != 0) return ret;
    if (out3) {
        byte = 0x03;
        ret = hmac_sha256(temp, out2, 32, &byte, 1, out3);
    }
    return ret;
}

/* ================================================================
 *  X25519 (mbedtls_ecp on Curve25519)
 * ================================================================ */

static int noise_rng(void *ctx, unsigned char *buf, size_t len)
{
    (void)ctx;
    esp_fill_random(buf, len);
    return 0;
}

/** RFC 7748 scalar clamping. */
static void x25519_clamp(uint8_t k[32])
{
    k[0] &= 248;
    k[31] &= 127;
    k[31] |= 64;
}

/** out = scalar * point (all 32-byte little-endian, RFC 7748). */
static int x25519(uint8_t out[32], const uint8_t priv[32], const uint8_t *point /* NULL = base point */)
{
    mbedtls_ecp_group grp;
    mbedtls_ecp_point P, R;
    mbedtls_mpi d;
    uint8_t k[32];
    int ret;
    size_t olen = 0;

    memcpy(k, priv, 32);
    x25519_clamp(k);

    mbedtls_ecp_group_init(&grp);
    mbedtls_ecp_point_init(&P);
    mbedtls_ecp_point_init(&R);
    mbedtls_mpi_init(&d);

    ret = mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_CURVE25519);
    if (ret == 0) ret = mbedtls_mpi_read_binary_le(&d, k, 32);
    if (ret == 0) {
        if (point) {
            ret = mbedtls_ecp_point_read_binary(&grp, &P, point, 32);
        } else {
            ret = mbedtls_ecp_copy(&P, &grp.G);
        }
    }
    if (ret == 0) ret = mbedtls_ecp_mul(&grp, &R, &d, &P, noise_rng, NULL);
    if (ret == 0) ret = mbedtls_ecp_point_write_binary(&grp, &R, MBEDTLS_ECP_PF_COMPRESSED,
                                                       &olen, out, 32);

    mbedtls_mpi_free(&d);
    mbedtls_ecp_point_free(&P);
    mbedtls_ecp_point_free(&R);
    mbedtls_ecp_group_free(&grp);

    if (ret == 0 && olen != 32) ret = -1;
    return ret;
}

esp_err_t hm_noise_x25519_public(const uint8_t priv[HM_NOISE_KEY_SIZE],
                                 uint8_t pub[HM_NOISE_KEY_SIZE])
{
    return (x25519(pub, priv, NULL) == 0) ? ESP_OK : ESP_FAIL;
}

/* ================================================================
 *  CipherState (ChaCha20-Poly1305)
 * ================================================================ */

static void cs_init_key(hm_noise_cipherstate_t *cs, const uint8_t key[32])
{
    memcpy(cs->key, key, 32);
    cs->has_key = true;
    cs->nonce = 0;
}

/** Nonce: 4 zero bytes || 64-bit little-endian counter (Noise spec §12.3). */
static void cs_nonce_bytes(uint64_t n, uint8_t out[12])
{
    memset(out, 0, 4);
    for (int i = 0; i < 8; i++) {
        out[4 + i] = (uint8_t)(n >> (8 * i));
    }
}

static int cs_encrypt(hm_noise_cipherstate_t *cs,
                      const uint8_t *ad, size_t ad_len,
                      const uint8_t *pt, size_t pt_len,
                      uint8_t *ct, uint8_t tag[HM_NOISE_TAG_SIZE])
{
    uint8_t nonce[12];
    mbedtls_chachapoly_context cp;
    int ret;

    if (cs->nonce == UINT64_MAX) {
        /* Noise reserved maximum — must rekey or reconnect before this. */
        return -1;
    }
    cs_nonce_bytes(cs->nonce, nonce);
    mbedtls_chachapoly_init(&cp);
    ret = mbedtls_chachapoly_setkey(&cp, cs->key);
    if (ret == 0) {
        ret = mbedtls_chachapoly_encrypt_and_tag(&cp, pt_len, nonce,
                                                 ad, ad_len, pt, ct, tag);
    }
    mbedtls_chachapoly_free(&cp);
    if (ret == 0) cs->nonce++;
    return ret;
}

static int cs_decrypt(hm_noise_cipherstate_t *cs,
                      const uint8_t *ad, size_t ad_len,
                      const uint8_t *ct, size_t ct_len,
                      const uint8_t tag[HM_NOISE_TAG_SIZE],
                      uint8_t *pt)
{
    uint8_t nonce[12];
    mbedtls_chachapoly_context cp;
    int ret;

    if (cs->nonce == UINT64_MAX) {
        return -1;
    }
    cs_nonce_bytes(cs->nonce, nonce);
    mbedtls_chachapoly_init(&cp);
    ret = mbedtls_chachapoly_setkey(&cp, cs->key);
    if (ret == 0) {
        ret = mbedtls_chachapoly_auth_decrypt(&cp, ct_len, nonce,
                                              ad, ad_len, tag, ct, pt);
    }
    mbedtls_chachapoly_free(&cp);
    /* The counter only advances on success: a failed decrypt is fatal for
     * the session and MUST NOT be retried under another nonce. */
    if (ret == 0) cs->nonce++;
    return ret;
}

/* ================================================================
 *  SymmetricState
 * ================================================================ */

static void mix_hash(hm_noise_ctx_t *ctx, const uint8_t *data, size_t len)
{
    sha256_2(ctx->h, HM_NOISE_HASH_SIZE, data, len, ctx->h);
}

static int mix_key(hm_noise_ctx_t *ctx, const uint8_t *ikm, size_t ikm_len)
{
    uint8_t temp_k[32];
    int ret = noise_hkdf(ctx->ck, ikm, ikm_len, ctx->ck, temp_k, NULL);
    if (ret == 0) cs_init_key(&ctx->hs_cipher, temp_k);
    return ret;
}

static int mix_key_and_hash(hm_noise_ctx_t *ctx, const uint8_t *ikm, size_t ikm_len)
{
    uint8_t temp_h[32], temp_k[32];
    int ret = noise_hkdf(ctx->ck, ikm, ikm_len, ctx->ck, temp_h, temp_k);
    if (ret != 0) return ret;
    mix_hash(ctx, temp_h, 32);
    cs_init_key(&ctx->hs_cipher, temp_k);
    return 0;
}

/** EncryptAndHash: appends ciphertext (+tag when keyed) to out. */
static int encrypt_and_hash(hm_noise_ctx_t *ctx,
                            const uint8_t *pt, size_t pt_len,
                            uint8_t *out, size_t out_sz, size_t *out_len)
{
    if (ctx->hs_cipher.has_key) {
        if (out_sz < pt_len + HM_NOISE_TAG_SIZE) return -1;
        int ret = cs_encrypt(&ctx->hs_cipher, ctx->h, HM_NOISE_HASH_SIZE,
                             pt, pt_len, out, out + pt_len);
        if (ret != 0) return ret;
        *out_len = pt_len + HM_NOISE_TAG_SIZE;
    } else {
        if (out_sz < pt_len) return -1;
        memcpy(out, pt, pt_len);
        *out_len = pt_len;
    }
    mix_hash(ctx, out, *out_len);
    return 0;
}

/** DecryptAndHash: ct includes the tag when keyed. */
static int decrypt_and_hash(hm_noise_ctx_t *ctx,
                            const uint8_t *ct, size_t ct_len,
                            uint8_t *pt, size_t pt_sz, size_t *pt_len)
{
    int ret;
    if (ctx->hs_cipher.has_key) {
        if (ct_len < HM_NOISE_TAG_SIZE) return -1;
        size_t body = ct_len - HM_NOISE_TAG_SIZE;
        if (body > pt_sz) return -1;
        ret = cs_decrypt(&ctx->hs_cipher, ctx->h, HM_NOISE_HASH_SIZE,
                         ct, body, ct + body, pt);
        if (ret != 0) return ret;
        *pt_len = body;
    } else {
        if (ct_len > pt_sz) return -1;
        memcpy(pt, ct, ct_len);
        *pt_len = ct_len;
    }
    mix_hash(ctx, ct, ct_len);
    return 0;
}

/** DH between one of our keys and a remote public key, then MixKey. */
static int mix_dh(hm_noise_ctx_t *ctx, const uint8_t priv[32], const uint8_t pub[32])
{
    uint8_t dh[32];
    if (x25519(dh, priv, pub) != 0) {
        return -1;
    }
    return mix_key(ctx, dh, 32);
}

static int split(hm_noise_ctx_t *ctx)
{
    uint8_t k1[32], k2[32];
    int ret = noise_hkdf(ctx->ck, NULL, 0, k1, k2, NULL);
    if (ret != 0) return ret;
    /* Initiator sends with the first key, receives with the second. */
    cs_init_key(&ctx->send, k1);
    cs_init_key(&ctx->recv, k2);
    memcpy(ctx->handshake_hash, ctx->h, HM_NOISE_HASH_SIZE);
    ctx->finished = true;
    return 0;
}

/* ================================================================
 *  Handshake initialization
 * ================================================================ */

esp_err_t hm_noise_init(hm_noise_ctx_t *ctx,
                        hm_noise_pattern_t pattern,
                        const uint8_t psk[HM_NOISE_KEY_SIZE],
                        const uint8_t *s_priv,
                        const uint8_t *rs_pub,
                        const uint8_t *prologue, size_t prologue_len,
                        const uint8_t *e_priv)
{
    if (!ctx || !psk) {
        return ESP_ERR_INVALID_ARG;
    }
    if (pattern == HM_NOISE_PATTERN_KKPSK0 && (!rs_pub || !s_priv)) {
        ESP_LOGE(TAG, "KKpsk0 requires both static keys");
        return ESP_ERR_INVALID_ARG;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->pattern = pattern;
    memcpy(ctx->psk, psk, HM_NOISE_KEY_SIZE);

    /* Static key */
    if (s_priv) {
        memcpy(ctx->s_priv, s_priv, HM_NOISE_KEY_SIZE);
    } else {
        esp_fill_random(ctx->s_priv, HM_NOISE_KEY_SIZE);
    }
    if (hm_noise_x25519_public(ctx->s_priv, ctx->s_pub) != ESP_OK) {
        return ESP_FAIL;
    }

    /* Ephemeral key (test injection or fresh random) */
    if (e_priv) {
        memcpy(ctx->e_priv, e_priv, HM_NOISE_KEY_SIZE);
    } else {
        esp_fill_random(ctx->e_priv, HM_NOISE_KEY_SIZE);
    }
    if (hm_noise_x25519_public(ctx->e_priv, ctx->e_pub) != ESP_OK) {
        return ESP_FAIL;
    }

    /* InitializeSymmetric(protocol_name): name > 32 bytes -> h = SHA-256(name) */
    const char *name = (pattern == HM_NOISE_PATTERN_KKPSK0)
                           ? NOISE_NAME_KKPSK0 : NOISE_NAME_XXPSK2;
    sha256_2((const uint8_t *)name, strlen(name), NULL, 0, ctx->h);
    memcpy(ctx->ck, ctx->h, HM_NOISE_HASH_SIZE);

    /* MixHash(prologue) */
    mix_hash(ctx, prologue ? prologue : (const uint8_t *)"", prologue_len);

    /* Pre-messages: KK has "-> s" then "<- s"; XX has none. */
    if (pattern == HM_NOISE_PATTERN_KKPSK0) {
        memcpy(ctx->rs, rs_pub, HM_NOISE_KEY_SIZE);
        ctx->has_rs = true;
        mix_hash(ctx, ctx->s_pub, HM_NOISE_KEY_SIZE);
        mix_hash(ctx, ctx->rs, HM_NOISE_KEY_SIZE);
    }

    return ESP_OK;
}

/* ================================================================
 *  Message processing (initiator)
 * ================================================================ */

/** Token "e" (writer side): append e.pub, MixHash, and (psk mode) MixKey. */
static int write_e(hm_noise_ctx_t *ctx, uint8_t *out)
{
    memcpy(out, ctx->e_pub, HM_NOISE_KEY_SIZE);
    mix_hash(ctx, ctx->e_pub, HM_NOISE_KEY_SIZE);
    /* psk handshake: MixKey(e.public_key) on every "e" (Noise spec §9.3) */
    return mix_key(ctx, ctx->e_pub, HM_NOISE_KEY_SIZE);
}

/** Token "e" (reader side). */
static int read_e(hm_noise_ctx_t *ctx, const uint8_t *in)
{
    memcpy(ctx->re, in, HM_NOISE_KEY_SIZE);
    ctx->has_re = true;
    mix_hash(ctx, ctx->re, HM_NOISE_KEY_SIZE);
    return mix_key(ctx, ctx->re, HM_NOISE_KEY_SIZE);
}

esp_err_t hm_noise_write_message(hm_noise_ctx_t *ctx,
                                 const uint8_t *payload, size_t payload_len,
                                 uint8_t *out, size_t out_sz, size_t *out_len)
{
    size_t w = 0, n = 0;
    int ret = 0;
    const uint8_t empty[1] = {0};

    if (!payload) {
        payload = empty;
        payload_len = 0;
    }
    *out_len = 0;

    if (ctx->finished) {
        return ESP_ERR_INVALID_STATE;
    }

    if (ctx->pattern == HM_NOISE_PATTERN_XXPSK2 && ctx->msg_index == 0) {
        /* XXpsk2 message 1: e */
        if (out_sz < HM_NOISE_KEY_SIZE + payload_len + HM_NOISE_TAG_SIZE) {
            return ESP_ERR_INVALID_SIZE;
        }
        ret = write_e(ctx, out);
        w = HM_NOISE_KEY_SIZE;
    } else if (ctx->pattern == HM_NOISE_PATTERN_XXPSK2 && ctx->msg_index == 2) {
        /* XXpsk2 message 3: s, se */
        if (out_sz < HM_NOISE_KEY_SIZE + 2 * HM_NOISE_TAG_SIZE + payload_len) {
            return ESP_ERR_INVALID_SIZE;
        }
        ret = encrypt_and_hash(ctx, ctx->s_pub, HM_NOISE_KEY_SIZE,
                               out, out_sz, &n);
        w = n;
        if (ret == 0) ret = mix_dh(ctx, ctx->s_priv, ctx->re); /* se */
    } else if (ctx->pattern == HM_NOISE_PATTERN_KKPSK0 && ctx->msg_index == 0) {
        /* KKpsk0 message 1: psk, e, es, ss */
        if (out_sz < HM_NOISE_KEY_SIZE + payload_len + HM_NOISE_TAG_SIZE) {
            return ESP_ERR_INVALID_SIZE;
        }
        ret = mix_key_and_hash(ctx, ctx->psk, HM_NOISE_KEY_SIZE); /* psk */
        if (ret == 0) ret = write_e(ctx, out);                    /* e */
        w = HM_NOISE_KEY_SIZE;
        if (ret == 0) ret = mix_dh(ctx, ctx->e_priv, ctx->rs);    /* es */
        if (ret == 0) ret = mix_dh(ctx, ctx->s_priv, ctx->rs);    /* ss */
    } else {
        ESP_LOGE(TAG, "write_message: bad state (pattern=%d, msg=%d)",
                 ctx->pattern, ctx->msg_index);
        return ESP_ERR_INVALID_STATE;
    }
    if (ret != 0) {
        return ESP_FAIL;
    }

    /* Payload */
    ret = encrypt_and_hash(ctx, payload, payload_len, out + w, out_sz - w, &n);
    if (ret != 0) {
        return ESP_FAIL;
    }
    w += n;
    *out_len = w;
    ctx->msg_index++;

    /* XXpsk2 message 3 completes the handshake on the initiator. */
    if (ctx->pattern == HM_NOISE_PATTERN_XXPSK2 && ctx->msg_index == 3) {
        if (split(ctx) != 0) {
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

esp_err_t hm_noise_read_message(hm_noise_ctx_t *ctx,
                                const uint8_t *msg, size_t msg_len,
                                uint8_t *payload_out, size_t payload_sz,
                                size_t *payload_len)
{
    size_t r = 0, n = 0;
    int ret = 0;
    uint8_t discard[64];

    if (!payload_out) {
        payload_out = discard;
        payload_sz = sizeof(discard);
    }
    if (payload_len) *payload_len = 0;

    if (ctx->finished || ctx->msg_index != 1) {
        return ESP_ERR_INVALID_STATE;
    }

    if (ctx->pattern == HM_NOISE_PATTERN_XXPSK2) {
        /* XXpsk2 message 2: e, ee, s, es, psk */
        if (msg_len < HM_NOISE_KEY_SIZE + (HM_NOISE_KEY_SIZE + HM_NOISE_TAG_SIZE)
                          + HM_NOISE_TAG_SIZE) {
            return ESP_ERR_INVALID_SIZE;
        }
        ret = read_e(ctx, msg);                                     /* e */
        r = HM_NOISE_KEY_SIZE;
        if (ret == 0) ret = mix_dh(ctx, ctx->e_priv, ctx->re);      /* ee */
        if (ret == 0) {                                             /* s */
            size_t rs_len = 0;
            ret = decrypt_and_hash(ctx, msg + r,
                                   HM_NOISE_KEY_SIZE + HM_NOISE_TAG_SIZE,
                                   ctx->rs, HM_NOISE_KEY_SIZE, &rs_len);
            if (ret == 0 && rs_len != HM_NOISE_KEY_SIZE) ret = -1;
            if (ret == 0) ctx->has_rs = true;
            r += HM_NOISE_KEY_SIZE + HM_NOISE_TAG_SIZE;
        }
        if (ret == 0) ret = mix_dh(ctx, ctx->e_priv, ctx->rs);      /* es (initiator) */
        if (ret == 0) ret = mix_key_and_hash(ctx, ctx->psk, HM_NOISE_KEY_SIZE); /* psk */
    } else {
        /* KKpsk0 message 2: e, ee, se */
        if (msg_len < HM_NOISE_KEY_SIZE + HM_NOISE_TAG_SIZE) {
            return ESP_ERR_INVALID_SIZE;
        }
        ret = read_e(ctx, msg);                                     /* e */
        r = HM_NOISE_KEY_SIZE;
        if (ret == 0) ret = mix_dh(ctx, ctx->e_priv, ctx->re);      /* ee */
        if (ret == 0) ret = mix_dh(ctx, ctx->s_priv, ctx->re);      /* se (initiator) */
    }
    if (ret != 0) {
        ESP_LOGE(TAG, "Noise handshake message 2 processing failed");
        return ESP_ERR_INVALID_STATE;
    }

    /* Payload */
    ret = decrypt_and_hash(ctx, msg + r, msg_len - r, payload_out, payload_sz, &n);
    if (ret != 0) {
        /* AEAD failure = wrong PSK / tampered prologue / wrong static key. */
        ESP_LOGE(TAG, "Noise handshake authentication FAILED "
                      "(wrong PSK or tampered negotiation)");
        return ESP_ERR_INVALID_STATE;
    }
    if (payload_len) *payload_len = n;
    ctx->msg_index++;

    /* KKpsk0 completes after message 2. */
    if (ctx->pattern == HM_NOISE_PATTERN_KKPSK0) {
        if (split(ctx) != 0) {
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

/* ================================================================
 *  Transport
 * ================================================================ */

esp_err_t hm_noise_encrypt(hm_noise_ctx_t *ctx,
                           const uint8_t *pt, size_t pt_len,
                           uint8_t *out, size_t out_sz, size_t *out_len)
{
    if (!ctx->finished) {
        return ESP_ERR_INVALID_STATE;
    }
    if (out_sz < pt_len + HM_NOISE_TAG_SIZE) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (cs_encrypt(&ctx->send, NULL, 0, pt, pt_len, out, out + pt_len) != 0) {
        return ESP_FAIL;
    }
    *out_len = pt_len + HM_NOISE_TAG_SIZE;
    return ESP_OK;
}

esp_err_t hm_noise_decrypt(hm_noise_ctx_t *ctx,
                           const uint8_t *ct, size_t ct_len,
                           uint8_t *out, size_t out_sz, size_t *out_len)
{
    if (!ctx->finished) {
        return ESP_ERR_INVALID_STATE;
    }
    if (ct_len < HM_NOISE_TAG_SIZE) {
        return ESP_ERR_INVALID_SIZE;
    }
    size_t body = ct_len - HM_NOISE_TAG_SIZE;
    if (body > out_sz) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (cs_decrypt(&ctx->recv, NULL, 0, ct, body, ct + body, out) != 0) {
        ESP_LOGE(TAG, "Noise transport message rejected "
                      "(tampered, replayed or out-of-order)");
        return ESP_ERR_INVALID_STATE;
    }
    *out_len = body;
    return ESP_OK;
}

/* ================================================================
 *  Helpers
 * ================================================================ */

esp_err_t hm_noise_key_from_hex(const char *hex, uint8_t out[HM_NOISE_KEY_SIZE])
{
    if (!hex || strlen(hex) != 2 * HM_NOISE_KEY_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < HM_NOISE_KEY_SIZE; i++) {
        unsigned int v;
        if (sscanf(hex + 2 * i, "%2x", &v) != 1) {
            return ESP_ERR_INVALID_ARG;
        }
        out[i] = (uint8_t)v;
    }
    return ESP_OK;
}
