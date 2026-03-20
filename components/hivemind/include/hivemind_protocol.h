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
    HM_STATE_READY          = 5,
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
