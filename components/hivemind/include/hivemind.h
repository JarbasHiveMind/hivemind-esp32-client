/**
 * @file hivemind.h
 * @brief HiveMind ESP32 Client — public API.
 *
 * Connects an ESP32 device as a HiveMind satellite via WebSocket.
 * Handles password handshake, AES-GCM/ChaCha20-Poly1305 encryption,
 * bus messages, binary audio, and auto-reconnect.
 */
#ifndef HIVEMIND_H
#define HIVEMIND_H

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "cJSON.h"
#include "hivemind_crypto.h"
#include "hivemind_binary.h"
#include "hivemind_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque client handle. */
typedef struct hm_client hm_client_t;

/** Client configuration. */
typedef struct {
    const char *host;         /**< Server hostname or IP. */
    uint16_t port;            /**< Server port (default 5678). */
    const char *username;     /**< Auth username. */
    const char *access_key;   /**< Auth access key. */
    const char *password;     /**< Handshake password for key derivation. */
    const char *site_id;      /**< Site identifier (e.g. "esp32-kitchen"). */
    hm_cipher_t preferred_cipher;  /**< Preferred cipher for negotiation. */
    uint32_t reconnect_ms;    /**< Auto-reconnect delay in ms (0 = disabled, default 5000). */
} hm_config_t;

/** Callback: bus message received. */
typedef void (*hm_on_bus_message_cb)(hm_client_t *client, const char *type,
                                      cJSON *data, cJSON *context);

/** Callback: binary data received. */
typedef void (*hm_on_binary_cb)(hm_client_t *client, hm_bin_type_t bin_type,
                                 const uint8_t *data, size_t len);

/** Callback: connection state changed. */
typedef void (*hm_on_state_cb)(hm_client_t *client, hm_state_t state);

/**
 * @brief Create and initialize a HiveMind client.
 *
 * @param client_out  Pointer to receive allocated client handle.
 * @param config      Client configuration (copied internally).
 * @return ESP_OK on success, ESP_ERR_NO_MEM on allocation failure.
 */
esp_err_t hm_client_init(hm_client_t **client_out, const hm_config_t *config);

/**
 * @brief Free a HiveMind client and all associated resources.
 */
void hm_client_free(hm_client_t *client);

/**
 * @brief Set callback for bus messages.
 */
void hm_client_set_bus_cb(hm_client_t *client, hm_on_bus_message_cb cb);

/**
 * @brief Set callback for binary data.
 */
void hm_client_set_binary_cb(hm_client_t *client, hm_on_binary_cb cb);

/**
 * @brief Set callback for state changes.
 */
void hm_client_set_state_cb(hm_client_t *client, hm_on_state_cb cb);

/**
 * @brief Connect to the HiveMind hub.
 *
 * Non-blocking. Handshake proceeds via WebSocket events.
 * State callback fires as handshake progresses.
 *
 * @return ESP_OK if connection attempt started.
 */
esp_err_t hm_client_connect(hm_client_t *client);

/**
 * @brief Disconnect from the hub.
 */
esp_err_t hm_client_disconnect(hm_client_t *client);

/**
 * @brief Send a text utterance to the hub.
 *
 * Convenience wrapper: sends a BUS message with type "recognizer_loop:utterance".
 *
 * @param client  Connected client (must be in READY state).
 * @param text    Utterance text.
 * @return ESP_OK on success.
 */
esp_err_t hm_send_utterance(hm_client_t *client, const char *text);

/**
 * @brief Send a generic bus message.
 *
 * @param client   Connected client (must be in READY state).
 * @param type     Message type string (e.g. "recognizer_loop:utterance").
 * @param data     Data payload (may be NULL).
 * @param context  Context payload (may be NULL).
 * @return ESP_OK on success.
 */
esp_err_t hm_send_bus_message(hm_client_t *client, const char *type,
                               cJSON *data, cJSON *context);

/**
 * @brief Send binary data (e.g. audio).
 *
 * @param client    Connected client (must be in READY state).
 * @param bin_type  Binary payload type.
 * @param data      Raw binary data.
 * @param len       Length of data.
 * @return ESP_OK on success.
 */
esp_err_t hm_send_binary(hm_client_t *client, hm_bin_type_t bin_type,
                           const uint8_t *data, size_t len);

/**
 * @brief Get the current connection state.
 */
hm_state_t hm_client_get_state(const hm_client_t *client);

#ifdef __cplusplus
}
#endif

#endif /* HIVEMIND_H */
