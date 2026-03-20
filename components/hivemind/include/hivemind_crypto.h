/**
 * @file hivemind_crypto.h
 * @brief HiveMind cryptographic operations: hSub, PBKDF2, AES-GCM, ChaCha20-Poly1305.
 *
 * Uses mbedTLS (ESP-IDF built-in, HW-accelerated AES on ESP32).
 */
#ifndef HIVEMIND_CRYPTO_H
#define HIVEMIND_CRYPTO_H

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Cipher selection for negotiation. */
typedef enum {
    HM_CIPHER_AES_GCM = 0,
    HM_CIPHER_CHACHA20_POLY1305 = 1,
} hm_cipher_t;

/** JSON field encoding selection. */
typedef enum {
    HM_ENCODING_JSON_HEX = 0,
    HM_ENCODING_JSON_B64,
    HM_ENCODING_JSON_URLSAFE_B64,
    HM_ENCODING_JSON_B32,
    HM_ENCODING_JSON_Z85B,
    HM_ENCODING_JSON_Z85P,
    HM_ENCODING_JSON_B91,
} hm_encoding_t;

/** Nonce sizes per cipher. */
#define HM_AES_GCM_NONCE_SIZE       16
#define HM_CHACHA20_NONCE_SIZE      12
#define HM_AUTH_TAG_SIZE             16
#define HM_SESSION_KEY_SIZE          32
#define HM_HSUB_IV_SIZE              8
#define HM_HSUB_HEX_LEN             48
#define HM_PBKDF2_ITERATIONS    100000

/** Cipher context holding the derived session key, negotiated cipher, and encoding. */
typedef struct {
    uint8_t key[HM_SESSION_KEY_SIZE];
    hm_cipher_t cipher;
    hm_encoding_t encoding;
} hm_crypto_ctx_t;

/**
 * @brief Generate an hSub envelope.
 *
 * IV = 8 random bytes.
 * hsub = hex(IV + SHA256(IV + utf8(password)))[0:48]
 *
 * @param password  Null-terminated password string.
 * @param iv_out    Output buffer for the 8-byte IV (caller owns).
 * @param hsub_out  Output buffer for the 48-char hex string + null terminator (min 49 bytes).
 * @return ESP_OK on success.
 */
esp_err_t hm_crypto_generate_hsub(const char *password, uint8_t iv_out[HM_HSUB_IV_SIZE],
                                   char hsub_out[HM_HSUB_HEX_LEN + 1]);

/**
 * @brief Extract the 8-byte IV from a peer's hSub hex string.
 *
 * @param hsub_hex  48-char hex string from peer.
 * @param iv_out    Output buffer for 8 bytes.
 * @return ESP_OK on success.
 */
esp_err_t hm_crypto_extract_iv(const char *hsub_hex, uint8_t iv_out[HM_HSUB_IV_SIZE]);

/**
 * @brief Derive a 32-byte session key via PBKDF2-HMAC-SHA256.
 *
 * salt = XOR(client_iv, server_iv), 8 bytes.
 *
 * @param password   Null-terminated password.
 * @param client_iv  8-byte client IV.
 * @param server_iv  8-byte server IV.
 * @param ctx_out    Crypto context with derived key (cipher/encoding fields are not set here).
 * @return ESP_OK on success.
 */
esp_err_t hm_crypto_derive_key(const char *password,
                                const uint8_t client_iv[HM_HSUB_IV_SIZE],
                                const uint8_t server_iv[HM_HSUB_IV_SIZE],
                                hm_crypto_ctx_t *ctx_out);

/**
 * @brief Encrypt plaintext to JSON with the specified encoding.
 *
 * Output format: {"ciphertext":"encoded","tag":"encoded","nonce":"encoded"}
 *
 * @param ctx         Crypto context with key and cipher.
 * @param plaintext   Data to encrypt.
 * @param pt_len      Length of plaintext.
 * @param encoding    Encoding for JSON fields.
 * @param json_out    Output buffer for JSON string (caller owns).
 * @param json_out_sz Size of json_out buffer.
 * @return ESP_OK on success.
 */
esp_err_t hm_crypto_encrypt_json(const hm_crypto_ctx_t *ctx,
                                  const uint8_t *plaintext, size_t pt_len,
                                  hm_encoding_t encoding,
                                  char *json_out, size_t json_out_sz);

/**
 * @brief Decrypt a JSON encrypted message with the specified encoding.
 *
 * @param ctx          Crypto context with key and cipher.
 * @param json_str     JSON string with ciphertext, tag, nonce fields.
 * @param encoding     Encoding used for JSON fields.
 * @param pt_out       Output buffer for decrypted plaintext (caller owns).
 * @param pt_out_sz    Size of pt_out buffer.
 * @param pt_len_out   Actual decrypted length.
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE on auth failure.
 */
esp_err_t hm_crypto_decrypt_json(const hm_crypto_ctx_t *ctx,
                                  const char *json_str,
                                  hm_encoding_t encoding,
                                  uint8_t *pt_out, size_t pt_out_sz,
                                  size_t *pt_len_out);

/**
 * @brief Encrypt plaintext using negotiated cipher (JSON-HEX mode).
 *
 * Legacy wrapper — calls hm_crypto_encrypt_json with HM_ENCODING_JSON_HEX.
 *
 * @param ctx         Crypto context with key and cipher.
 * @param plaintext   Data to encrypt.
 * @param pt_len      Length of plaintext.
 * @param json_out    Output buffer for JSON string (caller owns). Must be large enough.
 * @param json_out_sz Size of json_out buffer.
 * @return ESP_OK on success.
 */
esp_err_t hm_crypto_encrypt_json_hex(const hm_crypto_ctx_t *ctx,
                                      const uint8_t *plaintext, size_t pt_len,
                                      char *json_out, size_t json_out_sz);

/**
 * @brief Decrypt a JSON-HEX encrypted message.
 *
 * Legacy wrapper — calls hm_crypto_decrypt_json with HM_ENCODING_JSON_HEX.
 *
 * @param ctx          Crypto context with key and cipher.
 * @param json_hex     JSON string with ciphertext, tag, nonce fields.
 * @param pt_out       Output buffer for decrypted plaintext (caller owns).
 * @param pt_out_sz    Size of pt_out buffer.
 * @param pt_len_out   Actual decrypted length.
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE on auth failure.
 */
esp_err_t hm_crypto_decrypt_json_hex(const hm_crypto_ctx_t *ctx,
                                      const char *json_hex,
                                      uint8_t *pt_out, size_t pt_out_sz,
                                      size_t *pt_len_out);

/**
 * @brief Encrypt plaintext to binary frame: nonce + ciphertext + tag.
 *
 * @param ctx        Crypto context.
 * @param plaintext  Data to encrypt.
 * @param pt_len     Length of plaintext.
 * @param out        Output buffer (nonce_size + pt_len + 16).
 * @param out_sz     Size of out buffer.
 * @param out_len    Actual output length.
 * @return ESP_OK on success.
 */
esp_err_t hm_crypto_encrypt_binary(const hm_crypto_ctx_t *ctx,
                                    const uint8_t *plaintext, size_t pt_len,
                                    uint8_t *out, size_t out_sz, size_t *out_len);

/**
 * @brief Decrypt a binary frame: nonce + ciphertext + tag.
 *
 * @param ctx        Crypto context.
 * @param frame      Binary frame.
 * @param frame_len  Length of frame.
 * @param pt_out     Output buffer for plaintext.
 * @param pt_out_sz  Size of pt_out.
 * @param pt_len_out Actual plaintext length.
 * @return ESP_OK on success.
 */
esp_err_t hm_crypto_decrypt_binary(const hm_crypto_ctx_t *ctx,
                                    const uint8_t *frame, size_t frame_len,
                                    uint8_t *pt_out, size_t pt_out_sz,
                                    size_t *pt_len_out);

/**
 * @brief Get the nonce size for a given cipher.
 */
static inline size_t hm_crypto_nonce_size(hm_cipher_t cipher) {
    return (cipher == HM_CIPHER_CHACHA20_POLY1305) ? HM_CHACHA20_NONCE_SIZE : HM_AES_GCM_NONCE_SIZE;
}

/**
 * @brief Get the cipher name string for protocol negotiation.
 */
static inline const char *hm_crypto_cipher_name(hm_cipher_t cipher) {
    return (cipher == HM_CIPHER_CHACHA20_POLY1305) ? "CHACHA20-POLY1305" : "AES-GCM";
}

/**
 * @brief Parse a cipher name string to enum.
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if unknown.
 */
esp_err_t hm_crypto_parse_cipher(const char *name, hm_cipher_t *cipher_out);

/**
 * @brief Get the protocol name for an encoding.
 * @return Static string, e.g. "JSON-HEX", "JSON-B64", etc.
 */
const char *hm_encoding_name(hm_encoding_t encoding);

/**
 * @brief Parse an encoding name string to enum.
 * @param name       Encoding name, e.g. "JSON-HEX".
 * @param encoding_out  Output enum value.
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if unknown.
 */
esp_err_t hm_encoding_parse(const char *name, hm_encoding_t *encoding_out);

#ifdef __cplusplus
}
#endif

#endif /* HIVEMIND_CRYPTO_H */
