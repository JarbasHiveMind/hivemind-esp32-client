/**
 * @file hivemind.c
 * @brief HiveMind ESP32 Client — WebSocket lifecycle and message dispatch.
 */

#include "hivemind.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_websocket_client.h>
#include <mbedtls/base64.h>

static const char *TAG = "hivemind";

/* --------------- Client struct --------------- */

struct hm_client {
    /* Config (owned copies) */
    char *host;
    char *username;
    char *access_key;
    char *password;
    char *site_id;
    uint16_t port;
    uint32_t reconnect_ms;

    /* Protocol state machine */
    hm_protocol_ctx_t protocol;

    /* WebSocket */
    esp_websocket_client_handle_t ws_handle;

    /* Callbacks */
    hm_on_bus_message_cb bus_cb;
    hm_on_binary_cb binary_cb;
    hm_on_state_cb state_cb;

    /* Reconnect timer */
    esp_timer_handle_t reconnect_timer;
};

/* --------------- Helpers --------------- */

/**
 * @brief Duplicate a string, returning NULL if input is NULL.
 */
static char *safe_strdup(const char *s)
{
    return s ? strdup(s) : NULL;
}

/**
 * @brief Notify state callback and update protocol state.
 */
static void notify_state(hm_client_t *client, hm_state_t new_state)
{
    hm_state_t old = client->protocol.state;
    if (old != new_state) {
        client->protocol.state = new_state;
        if (client->state_cb) {
            client->state_cb(client, new_state);
        }
    }
}

/**
 * @brief Reconnect timer callback.
 */
static void reconnect_timer_cb(void *arg)
{
    hm_client_t *client = (hm_client_t *)arg;
    ESP_LOGI(TAG, "Reconnect timer fired, attempting reconnect");
    hm_client_connect(client);
}

/**
 * @brief Start the reconnect timer if configured.
 */
static void start_reconnect_timer(hm_client_t *client)
{
    if (client->reconnect_ms == 0 || !client->reconnect_timer) {
        return;
    }
    esp_timer_stop(client->reconnect_timer); /* Ignore error if not running */
    esp_err_t err = esp_timer_start_once(client->reconnect_timer,
                                          (uint64_t)client->reconnect_ms * 1000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start reconnect timer: %s", esp_err_to_name(err));
    }
}

/* --------------- WebSocket event handler --------------- */

static void ws_event_handler(void *handler_args, esp_event_base_t base,
                              int32_t event_id, void *event_data)
{
    hm_client_t *client = (hm_client_t *)handler_args;
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "WebSocket connected");
        /* Protocol already in CONNECTING state from init */
        if (client->state_cb) {
            client->state_cb(client, client->protocol.state);
        }
        break;

    case WEBSOCKET_EVENT_DATA:
        if (data->op_code == 0x01) {
            /* Text frame */
            if (client->protocol.state < HM_STATE_READY) {
                /* Handshake in progress */
                char *reply = NULL;
                hm_state_t old_state = client->protocol.state;
                esp_err_t err = hm_protocol_handle_message(&client->protocol,
                                                            data->data_ptr, &reply);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Handshake error in state %d: %s",
                             old_state, esp_err_to_name(err));
                    break;
                }

                /* Notify state change */
                if (client->protocol.state != old_state && client->state_cb) {
                    client->state_cb(client, client->protocol.state);
                }

                /* Send reply if handshake produced one */
                if (reply) {
                    esp_websocket_client_send_text(client->ws_handle,
                                                    reply, strlen(reply),
                                                    portMAX_DELAY);
                    free(reply);
                }

                /* If we just became READY, notify */
                if (client->protocol.state == HM_STATE_READY && old_state != HM_STATE_READY) {
                    ESP_LOGI(TAG, "Handshake complete, READY");
                    if (client->state_cb) {
                        client->state_cb(client, HM_STATE_READY);
                    }
                }
            } else {
                /* READY state — decrypt and dispatch */
                hm_msg_type_t msg_type;
                cJSON *payload = NULL;
                cJSON *context = NULL;
                esp_err_t err = hm_protocol_decrypt_message(&client->protocol,
                                                             data->data_ptr,
                                                             &msg_type,
                                                             &payload, &context);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to decrypt message: %s", esp_err_to_name(err));
                    break;
                }

                if (client->bus_cb) {
                    const char *type_str = hm_msg_type_str(msg_type);

                    /* For BUS messages, extract the inner type and data */
                    if (msg_type == HM_MSG_BUS && payload) {
                        const cJSON *inner_type = cJSON_GetObjectItemCaseSensitive(payload, "type");
                        const cJSON *inner_data = cJSON_GetObjectItemCaseSensitive(payload, "data");
                        if (cJSON_IsString(inner_type)) {
                            type_str = inner_type->valuestring;
                        }
                        client->bus_cb(client, type_str,
                                       inner_data ? (cJSON *)inner_data : payload,
                                       context);
                    } else {
                        client->bus_cb(client, type_str, payload, context);
                    }
                }

                cJSON_Delete(payload);
                cJSON_Delete(context);
            }
        } else if (data->op_code == 0x02) {
            /* Binary frame */
            if (client->protocol.state != HM_STATE_READY) {
                ESP_LOGW(TAG, "Binary frame received before READY, ignoring");
                break;
            }

            /* Decrypt binary */
            size_t max_pt = data->data_len;
            uint8_t *pt_buf = (uint8_t *)malloc(max_pt);
            if (!pt_buf) {
                ESP_LOGE(TAG, "OOM decrypting binary");
                break;
            }
            size_t pt_len = 0;
            esp_err_t err = hm_crypto_decrypt_binary(&client->protocol.crypto,
                                                      (const uint8_t *)data->data_ptr,
                                                      data->data_len,
                                                      pt_buf, max_pt, &pt_len);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Binary decrypt failed: %s", esp_err_to_name(err));
                free(pt_buf);
                break;
            }

            /* Decode binary frame */
            hm_binary_frame_t frame;
            err = hm_binary_decode(pt_buf, pt_len, &frame);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Binary decode failed: %s", esp_err_to_name(err));
                free(pt_buf);
                break;
            }

            if (frame.msg_type == HM_MSG_BINARY && client->binary_cb) {
                client->binary_cb(client, frame.bin_type,
                                  frame.payload, frame.payload_len);
            } else if (client->bus_cb && frame.metadata && frame.metadata_len > 0) {
                /* Parse metadata as JSON bus message */
                char *meta_str = (char *)malloc(frame.metadata_len + 1);
                if (meta_str) {
                    memcpy(meta_str, frame.metadata, frame.metadata_len);
                    meta_str[frame.metadata_len] = '\0';
                    cJSON *meta = cJSON_Parse(meta_str);
                    free(meta_str);
                    if (meta) {
                        const char *type_str = hm_msg_type_str(frame.msg_type);
                        client->bus_cb(client, type_str, meta, NULL);
                        cJSON_Delete(meta);
                    }
                }
            }

            free(pt_buf);
        }
        break;

    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "WebSocket disconnected");
        notify_state(client, HM_STATE_DISCONNECTED);
        start_reconnect_timer(client);
        break;

    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGE(TAG, "WebSocket error");
        break;

    default:
        break;
    }
}

/* --------------- Public API --------------- */

esp_err_t hm_client_init(hm_client_t **client_out, const hm_config_t *config)
{
    hm_client_t *client = (hm_client_t *)calloc(1, sizeof(hm_client_t));
    if (!client) {
        ESP_LOGE(TAG, "Failed to allocate client");
        return ESP_ERR_NO_MEM;
    }

    /* Copy config strings */
    client->host = safe_strdup(config->host);
    client->username = safe_strdup(config->username);
    client->access_key = safe_strdup(config->access_key);
    client->password = safe_strdup(config->password);
    client->site_id = safe_strdup(config->site_id);
    client->port = config->port ? config->port : 5678;
    client->reconnect_ms = config->reconnect_ms ? config->reconnect_ms : 5000;

    /* Verify critical allocations */
    if (!client->host || !client->password) {
        ESP_LOGE(TAG, "Failed to duplicate config strings");
        hm_client_free(client);
        return ESP_ERR_NO_MEM;
    }

    /* Initialize protocol */
    hm_protocol_init(&client->protocol, client->password,
                      client->site_id, config->preferred_cipher);

    /* Create reconnect timer */
    esp_timer_create_args_t timer_args = {
        .callback = reconnect_timer_cb,
        .arg = client,
        .name = "hm_reconnect",
    };
    esp_err_t err = esp_timer_create(&timer_args, &client->reconnect_timer);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to create reconnect timer: %s", esp_err_to_name(err));
        /* Non-fatal: reconnect just won't work */
    }

    *client_out = client;
    ESP_LOGI(TAG, "Client initialized, host=%s:%u", client->host, client->port);
    return ESP_OK;
}

void hm_client_free(hm_client_t *client)
{
    if (!client) {
        return;
    }

    hm_client_disconnect(client);

    if (client->reconnect_timer) {
        esp_timer_stop(client->reconnect_timer);
        esp_timer_delete(client->reconnect_timer);
        client->reconnect_timer = NULL;
    }

    free(client->host);
    free(client->username);
    free(client->access_key);
    free(client->password);
    free(client->site_id);
    free(client);
}

void hm_client_set_bus_cb(hm_client_t *client, hm_on_bus_message_cb cb)
{
    client->bus_cb = cb;
}

void hm_client_set_binary_cb(hm_client_t *client, hm_on_binary_cb cb)
{
    client->binary_cb = cb;
}

void hm_client_set_state_cb(hm_client_t *client, hm_on_state_cb cb)
{
    client->state_cb = cb;
}

esp_err_t hm_client_connect(hm_client_t *client)
{
    if (client->ws_handle) {
        ESP_LOGW(TAG, "Already connected, disconnecting first");
        hm_client_disconnect(client);
    }

    /* Re-init protocol for fresh handshake */
    hm_protocol_init(&client->protocol, client->password,
                      client->site_id, client->protocol.preferred_cipher);

    /* Base64 encode "username:access_key" for authorization */
    char credentials[256];
    snprintf(credentials, sizeof(credentials), "%s:%s",
             client->username ? client->username : "",
             client->access_key ? client->access_key : "");

    size_t cred_len = strlen(credentials);
    size_t b64_len = 0;
    /* Calculate required output size */
    mbedtls_base64_encode(NULL, 0, &b64_len,
                           (const unsigned char *)credentials, cred_len);
    char *b64_buf = (char *)malloc(b64_len + 1);
    if (!b64_buf) {
        return ESP_ERR_NO_MEM;
    }
    mbedtls_base64_encode((unsigned char *)b64_buf, b64_len + 1, &b64_len,
                           (const unsigned char *)credentials, cred_len);
    b64_buf[b64_len] = '\0';

    /* Build WebSocket URL */
    char url[512];
    snprintf(url, sizeof(url), "ws://%s:%u?authorization=%s",
             client->host, client->port, b64_buf);
    free(b64_buf);

    esp_websocket_client_config_t ws_cfg = {
        .uri = url,
        .buffer_size = 8192,
    };

    client->ws_handle = esp_websocket_client_init(&ws_cfg);
    if (!client->ws_handle) {
        ESP_LOGE(TAG, "Failed to init websocket client");
        return ESP_FAIL;
    }

    esp_err_t err = esp_websocket_register_events(client->ws_handle,
                                                    WEBSOCKET_EVENT_ANY,
                                                    ws_event_handler,
                                                    client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register WS events: %s", esp_err_to_name(err));
        esp_websocket_client_destroy(client->ws_handle);
        client->ws_handle = NULL;
        return err;
    }

    err = esp_websocket_client_start(client->ws_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WS client: %s", esp_err_to_name(err));
        esp_websocket_client_destroy(client->ws_handle);
        client->ws_handle = NULL;
        return err;
    }

    ESP_LOGI(TAG, "Connecting to %s:%u", client->host, client->port);
    return ESP_OK;
}

esp_err_t hm_client_disconnect(hm_client_t *client)
{
    if (!client->ws_handle) {
        return ESP_OK;
    }

    /* Stop reconnect timer */
    if (client->reconnect_timer) {
        esp_timer_stop(client->reconnect_timer);
    }

    esp_websocket_client_stop(client->ws_handle);
    esp_websocket_client_destroy(client->ws_handle);
    client->ws_handle = NULL;

    notify_state(client, HM_STATE_DISCONNECTED);
    ESP_LOGI(TAG, "Disconnected");
    return ESP_OK;
}

hm_state_t hm_client_get_state(const hm_client_t *client)
{
    return client->protocol.state;
}

esp_err_t hm_send_utterance(hm_client_t *client, const char *text)
{
    cJSON *data = cJSON_CreateObject();
    if (!data) {
        return ESP_ERR_NO_MEM;
    }

    cJSON *utterances = cJSON_AddArrayToObject(data, "utterances");
    cJSON_AddItemToArray(utterances, cJSON_CreateString(text));
    cJSON_AddStringToObject(data, "lang", "en-us");

    esp_err_t err = hm_send_bus_message(client, "recognizer_loop:utterance", data, NULL);
    cJSON_Delete(data);
    return err;
}

esp_err_t hm_send_bus_message(hm_client_t *client, const char *type,
                               cJSON *data, cJSON *context)
{
    if (client->protocol.state != HM_STATE_READY) {
        ESP_LOGE(TAG, "Cannot send: not in READY state");
        return ESP_ERR_INVALID_STATE;
    }

    /* Build bus payload: {"type":"...", "data":{...}, "context":{...}} */
    cJSON *bus_payload = cJSON_CreateObject();
    if (!bus_payload) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(bus_payload, "type", type);
    if (data) {
        cJSON_AddItemReferenceToObject(bus_payload, "data", data);
    } else {
        cJSON_AddObjectToObject(bus_payload, "data");
    }
    if (context) {
        cJSON_AddItemReferenceToObject(bus_payload, "context", context);
    } else {
        cJSON_AddObjectToObject(bus_payload, "context");
    }

    char encrypted[8192];
    esp_err_t err = hm_protocol_encrypt_message(&client->protocol, "BUS",
                                                  bus_payload, encrypted, sizeof(encrypted));
    cJSON_Delete(bus_payload);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to encrypt bus message: %s", esp_err_to_name(err));
        return err;
    }

    int sent = esp_websocket_client_send_text(client->ws_handle,
                                               encrypted, strlen(encrypted),
                                               portMAX_DELAY);
    if (sent < 0) {
        ESP_LOGE(TAG, "Failed to send text frame");
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "Sent bus message: %s", type);
    return ESP_OK;
}

esp_err_t hm_send_binary(hm_client_t *client, hm_bin_type_t bin_type,
                           const uint8_t *data, size_t len)
{
    if (client->protocol.state != HM_STATE_READY) {
        ESP_LOGE(TAG, "Cannot send binary: not in READY state");
        return ESP_ERR_INVALID_STATE;
    }

    /* Encode binary frame */
    size_t frame_sz = len + 64; /* Header overhead */
    uint8_t *frame_buf = (uint8_t *)malloc(frame_sz);
    if (!frame_buf) {
        return ESP_ERR_NO_MEM;
    }

    size_t frame_len = 0;
    esp_err_t err = hm_binary_encode(HM_MSG_BINARY, bin_type,
                                      NULL, 0, data, len,
                                      frame_buf, frame_sz, &frame_len);
    if (err != ESP_OK) {
        free(frame_buf);
        return err;
    }

    /* Encrypt binary */
    size_t enc_sz = frame_len + HM_AES_GCM_NONCE_SIZE + HM_AUTH_TAG_SIZE + 16;
    uint8_t *enc_buf = (uint8_t *)malloc(enc_sz);
    if (!enc_buf) {
        free(frame_buf);
        return ESP_ERR_NO_MEM;
    }

    size_t enc_len = 0;
    err = hm_crypto_encrypt_binary(&client->protocol.crypto,
                                    frame_buf, frame_len,
                                    enc_buf, enc_sz, &enc_len);
    free(frame_buf);
    if (err != ESP_OK) {
        free(enc_buf);
        return err;
    }

    /* Send binary frame */
    int sent = esp_websocket_client_send_bin(client->ws_handle,
                                              (const char *)enc_buf, enc_len,
                                              portMAX_DELAY);
    free(enc_buf);

    if (sent < 0) {
        ESP_LOGE(TAG, "Failed to send binary frame");
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "Sent binary frame, type=%d, len=%zu", bin_type, len);
    return ESP_OK;
}
