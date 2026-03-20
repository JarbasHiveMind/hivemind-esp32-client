/**
 * @file hivemind_binary.h
 * @brief HiveMind bitstring binary protocol codec (V1).
 *
 * Encodes/decodes the binary frame format:
 *   pad(1) + versioned(1) + [version(8)] + type(5) + compressed(1)
 *   + metalen(8) + meta + [bintype(4)] + payload
 */
#ifndef HIVEMIND_BINARY_H
#define HIVEMIND_BINARY_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** HiveMind message types (5-bit, 0-13). */
typedef enum {
    HM_MSG_HANDSHAKE  = 0,
    HM_MSG_BUS        = 1,
    HM_MSG_SHARED_BUS = 2,
    HM_MSG_BROADCAST  = 3,
    HM_MSG_PROPAGATE  = 4,
    HM_MSG_ESCALATE   = 5,
    HM_MSG_HELLO      = 6,
    HM_MSG_QUERY      = 7,
    HM_MSG_CASCADE    = 8,
    HM_MSG_PING       = 9,
    HM_MSG_RENDEZVOUS = 10,
    HM_MSG_THIRDPARTY = 11,
    HM_MSG_BINARY     = 12,
    HM_MSG_PONG       = 13,
} hm_msg_type_t;

/** Binary payload types (4-bit, 0-6). */
typedef enum {
    HM_BIN_UNDEFINED       = 0,
    HM_BIN_RAW_AUDIO       = 1,
    HM_BIN_NUMPY_IMAGE     = 2,
    HM_BIN_FILE            = 3,
    HM_BIN_STT_TRANSCRIBE  = 4,
    HM_BIN_STT_HANDLE      = 5,
    HM_BIN_TTS_AUDIO       = 6,
} hm_bin_type_t;

/** Decoded binary frame. */
typedef struct {
    hm_msg_type_t msg_type;
    hm_bin_type_t bin_type;     /**< Only valid when msg_type == HM_MSG_BINARY. */
    bool versioned;
    uint8_t protocol_version;   /**< Only valid when versioned == true. */
    bool compressed;
    const uint8_t *metadata;    /**< Points into original frame buffer. */
    size_t metadata_len;
    const uint8_t *payload;     /**< Points into original frame buffer. */
    size_t payload_len;
} hm_binary_frame_t;

/**
 * @brief Encode a binary frame.
 *
 * @param msg_type      Message type.
 * @param bin_type      Binary payload type (ignored unless msg_type == HM_MSG_BINARY).
 * @param metadata      JSON metadata bytes (may be NULL if meta_len == 0).
 * @param meta_len      Length of metadata (max 255).
 * @param payload       Payload bytes.
 * @param payload_len   Length of payload.
 * @param out           Output buffer.
 * @param out_sz        Size of output buffer.
 * @param out_len       Actual encoded length.
 * @return ESP_OK on success.
 */
esp_err_t hm_binary_encode(hm_msg_type_t msg_type, hm_bin_type_t bin_type,
                            const uint8_t *metadata, size_t meta_len,
                            const uint8_t *payload, size_t payload_len,
                            uint8_t *out, size_t out_sz, size_t *out_len);

/**
 * @brief Decode a binary frame.
 *
 * Pointers in frame_out reference the input buffer — do not free input
 * while using frame_out.
 *
 * @param data       Raw frame bytes.
 * @param data_len   Length of frame.
 * @param frame_out  Decoded frame structure.
 * @return ESP_OK on success.
 */
esp_err_t hm_binary_decode(const uint8_t *data, size_t data_len,
                            hm_binary_frame_t *frame_out);

/**
 * @brief Get string name for a message type.
 */
const char *hm_msg_type_str(hm_msg_type_t type);

/**
 * @brief Parse a message type string to enum.
 */
esp_err_t hm_msg_type_parse(const char *name, hm_msg_type_t *type_out);

#ifdef __cplusplus
}
#endif

#endif /* HIVEMIND_BINARY_H */
