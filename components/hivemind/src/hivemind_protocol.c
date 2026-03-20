/**
 * @file hivemind_protocol.c
 * @brief HiveMind V1 handshake state machine and message envelope handling.
 */

#include "hivemind_protocol.h"

#include <string.h>
#include <stdio.h>
#include <esp_random.h>
#include <esp_log.h>
#include "cJSON.h"

static const char *TAG = "hm_protocol";

/* --------------- Helpers --------------- */

/**
 * @brief Generate a UUID v4 string from random bytes.
 *
 * @param out  Output buffer (min 37 bytes: 32 hex + 4 dashes + null).
 */
static void generate_uuid_v4(char out[37])
{
    uint8_t rnd[16];
    esp_fill_random(rnd, sizeof(rnd));

    /* Set version 4 (bits 12-15 of time_hi_and_version) */
    rnd[6] = (rnd[6] & 0x0F) | 0x40;
    /* Set variant 1 (bits 6-7 of clk_seq_hi_res) */
    rnd[8] = (rnd[8] & 0x3F) | 0x80;

    snprintf(out, 37,
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             rnd[0], rnd[1], rnd[2], rnd[3],
             rnd[4], rnd[5],
             rnd[6], rnd[7],
             rnd[8], rnd[9],
             rnd[10], rnd[11], rnd[12], rnd[13], rnd[14], rnd[15]);
}

/**
 * @brief Safely get a string field from a cJSON object.
 *
 * @param obj  Parent object.
 * @param key  Field name.
 * @return String value or NULL.
 */
static const char *json_get_string(const cJSON *obj, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(item) && item->valuestring) {
        return item->valuestring;
    }
    return NULL;
}

/**
 * @brief Safely get a bool field from a cJSON object.
 *
 * @param obj       Parent object.
 * @param key       Field name.
 * @param def_val   Default value if field missing.
 * @return Boolean value.
 */
static bool json_get_bool(const cJSON *obj, const char *key, bool def_val)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsBool(item)) {
        return cJSON_IsTrue(item);
    }
    return def_val;
}

/**
 * @brief Parse msg_type string from envelope and map to hm_msg_type_t.
 *
 * Handles both the protocol-level names (lowercase: "hello", "shake", "bus")
 * and the enum names (uppercase: "HELLO", "HANDSHAKE", "BUS").
 *
 * @param type_str  The msg_type string from JSON.
 * @param type_out  Parsed type.
 * @return ESP_OK on success.
 */
static esp_err_t parse_msg_type_flexible(const char *type_str, hm_msg_type_t *type_out)
{
    if (!type_str) {
        return ESP_ERR_INVALID_ARG;
    }
    /* Try standard enum parse first */
    if (hm_msg_type_parse(type_str, type_out) == ESP_OK) {
        return ESP_OK;
    }
    /* Handle lowercase protocol names used during handshake */
    if (strcasecmp(type_str, "hello") == 0) {
        *type_out = HM_MSG_HELLO;
        return ESP_OK;
    }
    if (strcasecmp(type_str, "shake") == 0 || strcasecmp(type_str, "handshake") == 0) {
        *type_out = HM_MSG_HANDSHAKE;
        return ESP_OK;
    }
    if (strcasecmp(type_str, "bus") == 0) {
        *type_out = HM_MSG_BUS;
        return ESP_OK;
    }
    ESP_LOGE(TAG, "Unknown msg_type during handshake: %s", type_str);
    return ESP_ERR_NOT_FOUND;
}

/* --------------- Public API --------------- */

void hm_protocol_init(hm_protocol_ctx_t *ctx, const char *password,
                       const char *site_id, hm_cipher_t preferred_cipher)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->password = password;
    ctx->site_id = site_id;
    ctx->preferred_cipher = preferred_cipher;
    ctx->binarize = false;
    ctx->state = HM_STATE_CONNECTING;
    generate_uuid_v4(ctx->session_id);
    ESP_LOGI(TAG, "Protocol init, session_id=%s", ctx->session_id);
}

esp_err_t hm_protocol_build_envelope(const char *msg_type, cJSON *payload,
                                      char *out, size_t out_sz)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(root, "msg_type", msg_type);
    if (payload) {
        cJSON_AddItemReferenceToObject(root, "payload", payload);
    } else {
        cJSON_AddObjectToObject(root, "payload");
    }
    cJSON_AddObjectToObject(root, "metadata");
    cJSON *route = cJSON_AddArrayToObject(root, "route");
    (void)route;
    cJSON_AddNullToObject(root, "node");
    cJSON_AddNullToObject(root, "target_site_id");
    cJSON_AddNullToObject(root, "target_pubkey");
    cJSON_AddNullToObject(root, "source_peer");

    char *printed = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!printed) {
        return ESP_ERR_NO_MEM;
    }

    size_t len = strlen(printed);
    if (len >= out_sz) {
        free(printed);
        ESP_LOGE(TAG, "Envelope buffer too small: need %zu, have %zu", len + 1, out_sz);
        return ESP_ERR_INVALID_SIZE;
    }

    memcpy(out, printed, len + 1);
    free(printed);
    return ESP_OK;
}

esp_err_t hm_protocol_build_encrypted_hello(hm_protocol_ctx_t *ctx,
                                             char *out, size_t out_sz)
{
    if (ctx->state != HM_STATE_KEY_DERIVED) {
        ESP_LOGE(TAG, "Cannot build encrypted hello in state %d", ctx->state);
        return ESP_ERR_INVALID_STATE;
    }

    /* Build HELLO payload: {pubkey:"", session:{session_id:uuid}, site_id:"..."} */
    cJSON *payload = cJSON_CreateObject();
    if (!payload) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(payload, "pubkey", "");
    cJSON *session = cJSON_AddObjectToObject(payload, "session");
    cJSON_AddStringToObject(session, "session_id", ctx->session_id);
    cJSON_AddStringToObject(payload, "site_id", ctx->site_id ? ctx->site_id : "unknown");

    /* Build envelope */
    char envelope[2048];
    esp_err_t err = hm_protocol_build_envelope("hello", payload, envelope, sizeof(envelope));
    cJSON_Delete(payload);
    if (err != ESP_OK) {
        return err;
    }

    /* Encrypt */
    err = hm_crypto_encrypt_json_hex(&ctx->crypto,
                                      (const uint8_t *)envelope, strlen(envelope),
                                      out, out_sz);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to encrypt HELLO");
        return err;
    }

    ctx->state = HM_STATE_READY;
    ESP_LOGI(TAG, "Handshake complete, state=READY");
    return ESP_OK;
}

esp_err_t hm_protocol_encrypt_message(hm_protocol_ctx_t *ctx,
                                       const char *msg_type, cJSON *payload,
                                       char *out, size_t out_sz)
{
    if (ctx->state != HM_STATE_READY) {
        ESP_LOGE(TAG, "Cannot encrypt in state %d", ctx->state);
        return ESP_ERR_INVALID_STATE;
    }

    char envelope[4096];
    esp_err_t err = hm_protocol_build_envelope(msg_type, payload, envelope, sizeof(envelope));
    if (err != ESP_OK) {
        return err;
    }

    return hm_crypto_encrypt_json_hex(&ctx->crypto,
                                       (const uint8_t *)envelope, strlen(envelope),
                                       out, out_sz);
}

esp_err_t hm_protocol_decrypt_message(hm_protocol_ctx_t *ctx,
                                       const char *encrypted,
                                       hm_msg_type_t *type_out,
                                       cJSON **payload_out,
                                       cJSON **context_out)
{
    if (ctx->state != HM_STATE_READY) {
        ESP_LOGE(TAG, "Cannot decrypt in state %d", ctx->state);
        return ESP_ERR_INVALID_STATE;
    }

    /* Decrypt */
    uint8_t pt_buf[4096];
    size_t pt_len = 0;
    esp_err_t err = hm_crypto_decrypt_json_hex(&ctx->crypto, encrypted,
                                                pt_buf, sizeof(pt_buf) - 1, &pt_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Decryption failed");
        return err;
    }
    pt_buf[pt_len] = '\0';

    /* Parse JSON envelope */
    cJSON *root = cJSON_Parse((const char *)pt_buf);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse decrypted JSON");
        return ESP_ERR_INVALID_RESPONSE;
    }

    /* Extract msg_type */
    const char *type_str = json_get_string(root, "msg_type");
    if (!type_str) {
        ESP_LOGE(TAG, "No msg_type in decrypted envelope");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    err = hm_msg_type_parse(type_str, type_out);
    if (err != ESP_OK) {
        /* Try flexible parse for protocol-level names */
        err = parse_msg_type_flexible(type_str, type_out);
        if (err != ESP_OK) {
            cJSON_Delete(root);
            return err;
        }
    }

    /* Extract payload */
    cJSON *payload_item = cJSON_GetObjectItemCaseSensitive(root, "payload");
    if (payload_item) {
        *payload_out = cJSON_Duplicate(payload_item, 1);
    } else {
        *payload_out = NULL;
    }

    /* Extract context (may be inside payload for bus messages) */
    if (context_out) {
        cJSON *ctx_item = cJSON_GetObjectItemCaseSensitive(root, "context");
        if (!ctx_item && payload_item) {
            ctx_item = cJSON_GetObjectItemCaseSensitive(payload_item, "context");
        }
        if (ctx_item) {
            *context_out = cJSON_Duplicate(ctx_item, 1);
        } else {
            *context_out = NULL;
        }
    }

    cJSON_Delete(root);
    return ESP_OK;
}

/* --------------- Handshake state machine --------------- */

/**
 * @brief Handle HELLO message from server (state CONNECTING -> HELLO_RECEIVED).
 */
static esp_err_t handle_hello(hm_protocol_ctx_t *ctx, const cJSON *payload)
{
    const char *pubkey = json_get_string(payload, "pubkey");
    const char *peer = json_get_string(payload, "peer");
    const char *node_id = json_get_string(payload, "node_id");

    if (pubkey) {
        strncpy(ctx->server_pubkey, pubkey, sizeof(ctx->server_pubkey) - 1);
        ctx->server_pubkey[sizeof(ctx->server_pubkey) - 1] = '\0';
    }
    if (peer) {
        strncpy(ctx->server_peer, peer, sizeof(ctx->server_peer) - 1);
        ctx->server_peer[sizeof(ctx->server_peer) - 1] = '\0';
    }
    if (node_id) {
        strncpy(ctx->server_node_id, node_id, sizeof(ctx->server_node_id) - 1);
        ctx->server_node_id[sizeof(ctx->server_node_id) - 1] = '\0';
    }

    ctx->state = HM_STATE_HELLO_RECEIVED;
    ESP_LOGI(TAG, "HELLO received, peer=%s node_id=%s",
             ctx->server_peer, ctx->server_node_id);
    return ESP_OK;
}

/**
 * @brief Handle first SHAKE from server (state HELLO_RECEIVED -> HANDSHAKE_SENT).
 *
 * Server sends shake with handshake:true. Client responds with its own SHAKE
 * containing envelope (hsub), preferred cipher, encodings.
 */
static esp_err_t handle_shake_request(hm_protocol_ctx_t *ctx, const cJSON *payload,
                                       char **reply_out)
{
    bool handshake = json_get_bool(payload, "handshake", false);
    if (!handshake) {
        ESP_LOGE(TAG, "Expected handshake:true in SHAKE");
        return ESP_ERR_INVALID_STATE;
    }

    /* Generate client hsub */
    esp_err_t err = hm_crypto_generate_hsub(ctx->password, ctx->client_iv, ctx->client_hsub);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to generate hSub");
        return err;
    }

    /* Build response SHAKE */
    cJSON *resp = cJSON_CreateObject();
    if (!resp) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(resp, "msg_type", "HANDSHAKE");

    cJSON *resp_payload = cJSON_AddObjectToObject(resp, "payload");
    cJSON_AddStringToObject(resp_payload, "envelope", ctx->client_hsub);
    cJSON_AddBoolToObject(resp_payload, "binarize", false);

    cJSON *encodings = cJSON_AddArrayToObject(resp_payload, "encodings");
    cJSON_AddItemToArray(encodings, cJSON_CreateString("JSON-HEX"));

    cJSON *ciphers = cJSON_AddArrayToObject(resp_payload, "ciphers");
    cJSON_AddItemToArray(ciphers, cJSON_CreateString(hm_crypto_cipher_name(ctx->preferred_cipher)));

    cJSON_AddObjectToObject(resp, "metadata");
    cJSON_AddArrayToObject(resp, "route");
    cJSON_AddNullToObject(resp, "node");
    cJSON_AddNullToObject(resp, "target_site_id");
    cJSON_AddNullToObject(resp, "target_pubkey");
    cJSON_AddNullToObject(resp, "source_peer");

    *reply_out = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);

    if (!*reply_out) {
        return ESP_ERR_NO_MEM;
    }

    ctx->state = HM_STATE_HANDSHAKE_SENT;
    ESP_LOGI(TAG, "SHAKE response sent, state=HANDSHAKE_SENT");
    return ESP_OK;
}

/**
 * @brief Handle second SHAKE from server (state HANDSHAKE_SENT -> KEY_DERIVED -> READY).
 *
 * Server sends its hsub in the envelope field. Client extracts server IV,
 * derives session key, then builds and sends an encrypted HELLO.
 */
static esp_err_t handle_shake_response(hm_protocol_ctx_t *ctx, const cJSON *payload,
                                        char **reply_out)
{
    const char *server_hsub = json_get_string(payload, "envelope");
    if (!server_hsub) {
        ESP_LOGE(TAG, "No envelope in server SHAKE response");
        return ESP_ERR_INVALID_RESPONSE;
    }

    /* Extract server IV from hsub */
    esp_err_t err = hm_crypto_extract_iv(server_hsub, ctx->server_iv);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to extract server IV");
        return err;
    }

    /* Derive session key */
    err = hm_crypto_derive_key(ctx->password, ctx->client_iv, ctx->server_iv, &ctx->crypto);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Key derivation failed");
        return err;
    }

    /* Set negotiated cipher — check if server specified one, otherwise use preferred */
    const cJSON *cipher_item = cJSON_GetObjectItemCaseSensitive(payload, "cipher");
    if (cJSON_IsString(cipher_item) && cipher_item->valuestring) {
        hm_cipher_t server_cipher;
        if (hm_crypto_parse_cipher(cipher_item->valuestring, &server_cipher) == ESP_OK) {
            ctx->crypto.cipher = server_cipher;
        } else {
            ctx->crypto.cipher = ctx->preferred_cipher;
        }
    } else {
        ctx->crypto.cipher = ctx->preferred_cipher;
    }

    ctx->state = HM_STATE_KEY_DERIVED;
    ESP_LOGI(TAG, "Key derived, cipher=%s", hm_crypto_cipher_name(ctx->crypto.cipher));

    /* Build encrypted HELLO */
    char *hello_buf = (char *)malloc(4096);
    if (!hello_buf) {
        return ESP_ERR_NO_MEM;
    }

    err = hm_protocol_build_encrypted_hello(ctx, hello_buf, 4096);
    if (err != ESP_OK) {
        free(hello_buf);
        return err;
    }

    *reply_out = hello_buf;
    return ESP_OK;
}

esp_err_t hm_protocol_handle_message(hm_protocol_ctx_t *ctx,
                                      const char *msg_json,
                                      char **reply_out)
{
    *reply_out = NULL;

    cJSON *root = cJSON_Parse(msg_json);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse incoming JSON");
        return ESP_ERR_INVALID_RESPONSE;
    }

    const char *type_str = json_get_string(root, "msg_type");
    if (!type_str) {
        ESP_LOGE(TAG, "No msg_type in message");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    hm_msg_type_t msg_type;
    esp_err_t err = parse_msg_type_flexible(type_str, &msg_type);
    if (err != ESP_OK) {
        cJSON_Delete(root);
        return err;
    }

    cJSON *payload = cJSON_GetObjectItemCaseSensitive(root, "payload");

    switch (ctx->state) {
    case HM_STATE_CONNECTING:
        if (msg_type != HM_MSG_HELLO) {
            ESP_LOGE(TAG, "Expected HELLO in CONNECTING state, got %s", type_str);
            err = ESP_ERR_INVALID_STATE;
        } else {
            err = handle_hello(ctx, payload ? payload : root);
        }
        break;

    case HM_STATE_HELLO_RECEIVED:
        if (msg_type != HM_MSG_HANDSHAKE) {
            ESP_LOGE(TAG, "Expected SHAKE in HELLO_RECEIVED state, got %s", type_str);
            err = ESP_ERR_INVALID_STATE;
        } else {
            err = handle_shake_request(ctx, payload ? payload : root, reply_out);
        }
        break;

    case HM_STATE_HANDSHAKE_SENT:
        if (msg_type != HM_MSG_HANDSHAKE) {
            ESP_LOGE(TAG, "Expected SHAKE in HANDSHAKE_SENT state, got %s", type_str);
            err = ESP_ERR_INVALID_STATE;
        } else {
            err = handle_shake_response(ctx, payload ? payload : root, reply_out);
        }
        break;

    default:
        ESP_LOGW(TAG, "Unexpected message in state %d", ctx->state);
        err = ESP_ERR_INVALID_STATE;
        break;
    }

    cJSON_Delete(root);
    return err;
}
