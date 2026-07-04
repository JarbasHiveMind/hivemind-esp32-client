/**
 * @file hivemind_protocol.h
 * @brief HiveMind V1 handshake state machine and message envelope handling.
 */
#ifndef HIVEMIND_PROTOCOL_H
#define HIVEMIND_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "cJSON.h"
#include "hivemind_crypto.h"
#include "hivemind_binary.h"
#include "hivemind_noise.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Handshake FSM states. */
typedef enum {
    HM_STATE_DISCONNECTED   = 0,
    HM_STATE_CONNECTING     = 1,
    HM_STATE_HELLO_RECEIVED = 2,
    HM_STATE_HANDSHAKE_SENT = 3,
    HM_STATE_KEY_DERIVED    = 4,
    HM_STATE_NOISE_HANDSHAKE_SENT = 5, /**< v3: Noise message 1 sent, waiting for message 2. */
    HM_STATE_READY          = 6,
} hm_state_t;

/** Protocol context — holds handshake and crypto state. */
typedef struct {
    hm_state_t state;
    hm_crypto_ctx_t crypto;

    /* Handshake data */
    uint8_t client_iv[HM_HSUB_IV_SIZE];
    uint8_t server_iv[HM_HSUB_IV_SIZE];
    char client_hsub[HM_HSUB_HEX_LEN + 1];

    /* Server info from HELLO */
    char server_pubkey[512];
    char server_peer[128];
    char server_node_id[128];

    /* Negotiated settings */
    bool binarize;
    hm_cipher_t preferred_cipher;    /**< Client preference, sent in SHAKE. */
    hm_encoding_t preferred_encoding; /**< Client preference, sent in SHAKE. */

    /* Config */
    const char *password;
    const char *site_id;
    char session_id[37];  /**< UUID v4 string. */

    /* --- Protocol v3 (Noise) — HIVEMIND-CRYPTO-1 §3.4 --- */
    bool v3_enabled;              /**< A 32-byte PSK is provisioned. */
    uint8_t psk[HM_NOISE_KEY_SIZE];
    uint8_t static_key[HM_NOISE_KEY_SIZE]; /**< Own X25519 static private key. */
    bool has_server_static_key;   /**< Server key pinned/provisioned (enables KKpsk0 + pin check). */
    uint8_t server_static_key[HM_NOISE_KEY_SIZE]; /**< Server X25519 static public key. */
    bool use_noise;               /**< True once a v3 Noise session is established. */
    hm_noise_ctx_t noise;
    char *server_hello_canon;     /**< Canonical JSON of the server HELLO payload (prologue). */
    uint8_t *pending_bin;         /**< Binary frame to send after the text reply (owned). */
    size_t pending_bin_len;
} hm_protocol_ctx_t;

/**
 * @brief Initialize the protocol context.
 *
 * @param ctx              Protocol context to initialize.
 * @param password         Password for handshake (must outlive ctx).
 * @param site_id          Site ID string (must outlive ctx).
 * @param preferred_cipher Client's preferred cipher.
 */
void hm_protocol_init(hm_protocol_ctx_t *ctx, const char *password,
                       const char *site_id, hm_cipher_t preferred_cipher);

/**
 * @brief Enable protocol v3 (Noise handshake) with a provisioned PSK.
 *
 * Call after hm_protocol_init(). The 32-byte PSK is provisioned, not derived
 * on-device (HIVEMIND-CRYPTO-1 §3.4.4): compute it once on a capable host as
 * argon2id(password, SHA-256(server node_id)) and flash it.
 *
 * @param ctx                Protocol context.
 * @param psk                32-byte pre-shared key.
 * @param static_priv        32-byte X25519 static private key (required —
 *                           generate once and persist so the server's
 *                           TOFU pin of this device stays stable).
 * @param server_static_pub  Server's 32-byte X25519 static public key, or
 *                           NULL. When set it acts as the pinned key
 *                           (XXpsk2 pin check) and enables KKpsk0.
 */
void hm_protocol_set_v3(hm_protocol_ctx_t *ctx,
                        const uint8_t psk[HM_NOISE_KEY_SIZE],
                        const uint8_t static_priv[HM_NOISE_KEY_SIZE],
                        const uint8_t *server_static_pub);

/**
 * @brief Free heap state owned by the protocol context.
 *
 * Call before re-initializing an already-used context and on teardown.
 */
void hm_protocol_deinit(hm_protocol_ctx_t *ctx);

/**
 * @brief Serialize a cJSON tree as canonical JSON (malloc'd, caller frees).
 *
 * Matches Python's json.dumps(payload, sort_keys=True,
 * separators=(",", ":"), ensure_ascii=False): object keys sorted bytewise,
 * compact separators, UTF-8 passthrough. Used for the Noise prologue
 * (HIVEMIND-CRYPTO-1 §3.4.3), where both peers must derive identical bytes.
 *
 * @return Malloc'd string or NULL on error.
 */
char *hm_protocol_canonical_json(const cJSON *item);

/**
 * @brief Encrypt one payload as a v3 Noise transport frame.
 *
 * Prepends the frame marker (0x00 = JSON HiveMessage, 0x01 = WIRE-1 binary
 * frame) and encrypts under the send CipherState.
 *
 * @param ctx        Protocol context (READY, use_noise).
 * @param payload    Serialized JSON envelope or binary frame.
 * @param len        Payload length.
 * @param is_binary  True for a WIRE-1 binary frame.
 * @param out        Output buffer (needs len + 1 + 16 bytes).
 * @param out_sz     Size of out.
 * @param out_len    Ciphertext length.
 * @return ESP_OK on success.
 */
esp_err_t hm_protocol_noise_encrypt_frame(hm_protocol_ctx_t *ctx,
                                          const uint8_t *payload, size_t len,
                                          bool is_binary,
                                          uint8_t *out, size_t out_sz,
                                          size_t *out_len);

/**
 * @brief Decrypt one incoming v3 Noise transport frame.
 *
 * @param ctx        Protocol context (READY, use_noise).
 * @param frame      Received ciphertext.
 * @param frame_len  Ciphertext length.
 * @param out        Output buffer for the inner payload (marker stripped).
 * @param out_sz     Size of out.
 * @param out_len    Payload length.
 * @param is_binary  Set true when the frame carried a WIRE-1 binary frame.
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE on AEAD failure
 *         (tampering/replay/reordering — fatal for the session).
 */
esp_err_t hm_protocol_noise_decrypt_frame(hm_protocol_ctx_t *ctx,
                                          const uint8_t *frame, size_t frame_len,
                                          uint8_t *out, size_t out_sz,
                                          size_t *out_len, bool *is_binary);

/**
 * @brief Parse a decrypted HiveMessage envelope JSON string.
 *
 * @param json         Envelope JSON.
 * @param type_out     Parsed message type.
 * @param payload_out  Parsed payload (caller must cJSON_Delete).
 * @param context_out  Parsed context (caller must cJSON_Delete, may be NULL).
 * @return ESP_OK on success.
 */
esp_err_t hm_protocol_parse_envelope(const char *json,
                                     hm_msg_type_t *type_out,
                                     cJSON **payload_out,
                                     cJSON **context_out);

/**
 * @brief Process an incoming message during handshake.
 *
 * Call this for every received text message until state == HM_STATE_READY.
 *
 * @param ctx        Protocol context.
 * @param msg_json   Raw JSON string received from server.
 * @param reply_out  If non-NULL on return, caller must send this string and then free it.
 * @return ESP_OK if handshake progressed, ESP_ERR_INVALID_STATE on protocol error.
 */
esp_err_t hm_protocol_handle_message(hm_protocol_ctx_t *ctx,
                                      const char *msg_json,
                                      char **reply_out);

/**
 * @brief Build the encrypted HELLO message (final handshake step).
 *
 * Called internally by handle_message when key is derived.
 *
 * @param ctx       Protocol context (must be in KEY_DERIVED state).
 * @param out       Output buffer for encrypted JSON string.
 * @param out_sz    Size of output buffer.
 * @return ESP_OK on success.
 */
esp_err_t hm_protocol_build_encrypted_hello(hm_protocol_ctx_t *ctx,
                                             char *out, size_t out_sz);

/**
 * @brief Build a HiveMessage envelope JSON string.
 *
 * @param msg_type  Message type string ("bus", "hello", etc.).
 * @param payload   Payload cJSON object (ownership NOT transferred).
 * @param out       Output buffer.
 * @param out_sz    Size of output buffer.
 * @return ESP_OK on success.
 */
esp_err_t hm_protocol_build_envelope(const char *msg_type, cJSON *payload,
                                      char *out, size_t out_sz);

/**
 * @brief Encrypt and wrap a message for sending.
 *
 * Builds envelope, encrypts with session key, returns ready-to-send string.
 *
 * @param ctx       Protocol context (must be READY).
 * @param msg_type  Message type string.
 * @param payload   Payload cJSON object.
 * @param out       Output buffer for encrypted JSON.
 * @param out_sz    Size of output buffer.
 * @return ESP_OK on success.
 */
esp_err_t hm_protocol_encrypt_message(hm_protocol_ctx_t *ctx,
                                       const char *msg_type, cJSON *payload,
                                       char *out, size_t out_sz);

/**
 * @brief Decrypt a received message and parse the envelope.
 *
 * @param ctx        Protocol context (must be READY).
 * @param encrypted  Encrypted JSON-HEX string from server.
 * @param type_out   Parsed message type.
 * @param payload_out Parsed payload (caller must cJSON_Delete).
 * @param context_out Parsed context (caller must cJSON_Delete, may be NULL).
 * @return ESP_OK on success.
 */
esp_err_t hm_protocol_decrypt_message(hm_protocol_ctx_t *ctx,
                                       const char *encrypted,
                                       hm_msg_type_t *type_out,
                                       cJSON **payload_out,
                                       cJSON **context_out);

#ifdef __cplusplus
}
#endif

#endif /* HIVEMIND_PROTOCOL_H */
