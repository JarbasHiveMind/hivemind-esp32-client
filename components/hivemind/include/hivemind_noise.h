/**
 * @file hivemind_noise.h
 * @brief HiveMind protocol v3 — Noise handshake (HIVEMIND-CRYPTO-1 §3.4).
 *
 * Client-side (initiator) implementation of the two registered handshake
 * patterns over the mandatory cipher suite:
 *
 * - Noise_XXpsk2_25519_ChaChaPoly_SHA256 — general case: static keys are
 *   exchanged inside the handshake, the provisioned 32-byte PSK authenticates
 *   it. TOFU-then-pin the learned server static key.
 * - Noise_KKpsk0_25519_ChaChaPoly_SHA256 — pre-provisioned case: both static
 *   public keys are known in advance (2 handshake messages instead of 3).
 *
 * All primitives come from mbedTLS: X25519 (mbedtls_ecp on
 * MBEDTLS_ECP_DP_CURVE25519), ChaCha20-Poly1305 (mbedtls_chachapoly),
 * SHA-256 and HMAC-SHA256 (Noise HKDF).
 *
 * The PSK is **provisioned**, not derived on-device: argon2id (the
 * HIVEMIND-CRYPTO-1 §3.4.4 KDF) is infeasible on a microcontroller, so the
 * 32-byte PSK is computed once on a capable host
 * (`argon2id(password, SHA-256(server node_id))`, e.g. via the
 * `hivemind-core` PSK derivation command) and flashed into the device
 * configuration.
 */
#ifndef HIVEMIND_NOISE_H
#define HIVEMIND_NOISE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HM_NOISE_KEY_SIZE   32  /**< X25519 keys, PSK, hashes — all 32 bytes. */
#define HM_NOISE_TAG_SIZE   16  /**< Poly1305 tag. */
#define HM_NOISE_HASH_SIZE  32  /**< SHA-256. */

/** Registered handshake patterns (HIVEMIND-CRYPTO-1 §3.4.2). */
typedef enum {
    HM_NOISE_PATTERN_XXPSK2 = 0, /**< "XXpsk2" — general case, MUST support. */
    HM_NOISE_PATTERN_KKPSK0 = 1, /**< "KKpsk0" — pre-provisioned static keys. */
} hm_noise_pattern_t;

/** Pattern name as it appears on the wire (e.g. "XXpsk2"). */
const char *hm_noise_pattern_name(hm_noise_pattern_t pattern);

/** The mandatory suite name as it appears on the wire. */
#define HM_NOISE_SUITE_CHACHA "25519_ChaChaPoly_SHA256"

/** One direction of post-Split() transport encryption. */
typedef struct {
    uint8_t key[HM_NOISE_KEY_SIZE];
    bool has_key;
    uint64_t nonce; /**< Strictly sequential; never reused or rewound. */
} hm_noise_cipherstate_t;

/** A Noise handshake in progress (client = initiator) and, once finished,
 *  the two transport CipherStates. */
typedef struct {
    hm_noise_pattern_t pattern;
    int msg_index;      /**< Next handshake message ordinal (0-based). */
    bool finished;      /**< True after Split(). */

    /* SymmetricState */
    uint8_t h[HM_NOISE_HASH_SIZE];   /**< Handshake hash. */
    uint8_t ck[HM_NOISE_HASH_SIZE];  /**< Chaining key. */
    hm_noise_cipherstate_t hs_cipher; /**< Handshake-phase CipherState. */

    /* Keys */
    uint8_t s_priv[HM_NOISE_KEY_SIZE], s_pub[HM_NOISE_KEY_SIZE];
    uint8_t e_priv[HM_NOISE_KEY_SIZE], e_pub[HM_NOISE_KEY_SIZE];
    uint8_t rs[HM_NOISE_KEY_SIZE];   /**< Remote static (provisioned for KK, learned for XX). */
    bool has_rs;
    uint8_t re[HM_NOISE_KEY_SIZE];   /**< Remote ephemeral. */
    bool has_re;
    uint8_t psk[HM_NOISE_KEY_SIZE];

    /* Post-Split() transport */
    hm_noise_cipherstate_t send;  /**< Initiator -> responder. */
    hm_noise_cipherstate_t recv;  /**< Responder -> initiator. */
    uint8_t handshake_hash[HM_NOISE_HASH_SIZE]; /**< Final h, for channel binding. */
} hm_noise_ctx_t;

/**
 * @brief Compute an X25519 public key from a private key.
 */
esp_err_t hm_noise_x25519_public(const uint8_t priv[HM_NOISE_KEY_SIZE],
                                 uint8_t pub[HM_NOISE_KEY_SIZE]);

/**
 * @brief Initialize a Noise handshake as the **initiator** (the node/client).
 *
 * @param ctx           Context to initialize.
 * @param pattern       Handshake pattern.
 * @param psk           Provisioned 32-byte PSK (HIVEMIND-CRYPTO-1 §3.4.4).
 * @param s_priv        Static X25519 private key, or NULL to generate one.
 * @param rs_pub        Remote (server) static public key — required for
 *                      KKpsk0, ignored for XXpsk2.
 * @param prologue      Prologue bytes (HIVEMIND-CRYPTO-1 §3.4.3) — may be NULL/0.
 * @param prologue_len  Length of prologue.
 * @param e_priv        Ephemeral private key override for tests, or NULL to
 *                      generate a fresh random ephemeral (production path).
 * @return ESP_OK on success.
 */
esp_err_t hm_noise_init(hm_noise_ctx_t *ctx,
                        hm_noise_pattern_t pattern,
                        const uint8_t psk[HM_NOISE_KEY_SIZE],
                        const uint8_t *s_priv,
                        const uint8_t *rs_pub,
                        const uint8_t *prologue, size_t prologue_len,
                        const uint8_t *e_priv);

/**
 * @brief Produce the next handshake message (initiator messages 1 and 3).
 *
 * @param ctx          Handshake context.
 * @param payload      Application payload to piggyback (may be NULL/0).
 * @param payload_len  Length of payload.
 * @param out          Output buffer for the handshake message.
 * @param out_sz       Size of out.
 * @param out_len      Actual message length.
 * @return ESP_OK on success. When the handshake completes, Split() runs
 *         automatically and ctx->finished becomes true.
 */
esp_err_t hm_noise_write_message(hm_noise_ctx_t *ctx,
                                 const uint8_t *payload, size_t payload_len,
                                 uint8_t *out, size_t out_sz, size_t *out_len);

/**
 * @brief Consume a handshake message from the responder (message 2).
 *
 * Any failure (AEAD auth failure inside the handshake = wrong PSK, tampered
 * negotiation/prologue, wrong static key) is fatal for the connection.
 *
 * @param ctx          Handshake context.
 * @param msg          Received handshake message bytes.
 * @param msg_len      Length of msg.
 * @param payload_out  Buffer for the peer's application payload (may be NULL).
 * @param payload_sz   Size of payload_out.
 * @param payload_len  Actual payload length.
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE on authentication failure.
 */
esp_err_t hm_noise_read_message(hm_noise_ctx_t *ctx,
                                const uint8_t *msg, size_t msg_len,
                                uint8_t *payload_out, size_t payload_sz,
                                size_t *payload_len);

/**
 * @brief Encrypt one transport message (after Split()).
 *
 * Output is ciphertext + 16-byte tag; the nonce is the implicit sequential
 * counter (never transmitted, never reused).
 */
esp_err_t hm_noise_encrypt(hm_noise_ctx_t *ctx,
                           const uint8_t *pt, size_t pt_len,
                           uint8_t *out, size_t out_sz, size_t *out_len);

/**
 * @brief Decrypt one transport message (after Split()).
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE on AEAD failure —
 *         tampering, replay, or reordering; fatal for the session, the
 *         message MUST NOT be retried under another nonce.
 */
esp_err_t hm_noise_decrypt(hm_noise_ctx_t *ctx,
                           const uint8_t *ct, size_t ct_len,
                           uint8_t *out, size_t out_sz, size_t *out_len);

/**
 * @brief Parse a 64-char hex string into 32 key bytes.
 *
 * Convenience for provisioning fields (PSK / static keys) supplied as hex.
 */
esp_err_t hm_noise_key_from_hex(const char *hex, uint8_t out[HM_NOISE_KEY_SIZE]);

#ifdef __cplusplus
}
#endif

#endif /* HIVEMIND_NOISE_H */
