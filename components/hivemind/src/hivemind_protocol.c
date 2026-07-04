/**
 * @file hivemind_protocol.c
 * @brief HiveMind V1 handshake state machine and message envelope handling.
 */

#include "hivemind_protocol.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
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

/* --------------- Canonical JSON (Noise prologue) --------------- */

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
    bool err;
} canon_buf_t;

static void canon_put(canon_buf_t *b, const char *data, size_t len)
{
    if (b->err) {
        return;
    }
    if (b->len + len + 1 > b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 256;
        while (cap < b->len + len + 1) {
            cap *= 2;
        }
        char *nb = (char *)realloc(b->buf, cap);
        if (!nb) {
            b->err = true;
            return;
        }
        b->buf = nb;
        b->cap = cap;
    }
    memcpy(b->buf + b->len, data, len);
    b->len += len;
    b->buf[b->len] = '\0';
}

static void canon_puts(canon_buf_t *b, const char *s)
{
    canon_put(b, s, strlen(s));
}

/** JSON string escaping matching Python json.dumps(ensure_ascii=False). */
static void canon_put_string(canon_buf_t *b, const char *s)
{
    canon_puts(b, "\"");
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        switch (*p) {
        case '"':  canon_puts(b, "\\\""); break;
        case '\\': canon_puts(b, "\\\\"); break;
        case '\b': canon_puts(b, "\\b");  break;
        case '\f': canon_puts(b, "\\f");  break;
        case '\n': canon_puts(b, "\\n");  break;
        case '\r': canon_puts(b, "\\r");  break;
        case '\t': canon_puts(b, "\\t");  break;
        default:
            if (*p < 0x20) {
                char esc[8];
                snprintf(esc, sizeof(esc), "\\u%04x", *p);
                canon_puts(b, esc);
            } else {
                canon_put(b, (const char *)p, 1);
            }
        }
    }
    canon_puts(b, "\"");
}

static int canon_key_cmp(const void *a, const void *b)
{
    const cJSON *ia = *(const cJSON *const *)a;
    const cJSON *ib = *(const cJSON *const *)b;
    return strcmp(ia->string ? ia->string : "", ib->string ? ib->string : "");
}

static void canon_print(canon_buf_t *b, const cJSON *item)
{
    if (b->err || !item) {
        b->err = true;
        return;
    }
    if (cJSON_IsNull(item)) {
        canon_puts(b, "null");
    } else if (cJSON_IsBool(item)) {
        canon_puts(b, cJSON_IsTrue(item) ? "true" : "false");
    } else if (cJSON_IsNumber(item)) {
        char num[64];
        double d = item->valuedouble;
        /* Match Python's int serialization; negotiation payloads use
         * integers only — floats are printed with %.17g best effort. */
        if (d == (double)(long long)d) {
            snprintf(num, sizeof(num), "%lld", (long long)d);
        } else {
            snprintf(num, sizeof(num), "%.17g", d);
        }
        canon_puts(b, num);
    } else if (cJSON_IsString(item)) {
        canon_put_string(b, item->valuestring ? item->valuestring : "");
    } else if (cJSON_IsArray(item)) {
        canon_puts(b, "[");
        bool first = true;
        for (const cJSON *c = item->child; c; c = c->next) {
            if (!first) {
                canon_puts(b, ",");
            }
            first = false;
            canon_print(b, c);
        }
        canon_puts(b, "]");
    } else if (cJSON_IsObject(item)) {
        size_t n = 0;
        for (const cJSON *c = item->child; c; c = c->next) {
            n++;
        }
        const cJSON **items = NULL;
        if (n > 0) {
            items = (const cJSON **)malloc(n * sizeof(*items));
            if (!items) {
                b->err = true;
                return;
            }
            size_t i = 0;
            for (const cJSON *c = item->child; c; c = c->next) {
                items[i++] = c;
            }
            qsort(items, n, sizeof(*items), canon_key_cmp);
        }
        canon_puts(b, "{");
        for (size_t i = 0; i < n; i++) {
            if (i > 0) {
                canon_puts(b, ",");
            }
            canon_put_string(b, items[i]->string ? items[i]->string : "");
            canon_puts(b, ":");
            canon_print(b, items[i]);
        }
        canon_puts(b, "}");
        free((void *)items);
    } else {
        b->err = true;
    }
}

char *hm_protocol_canonical_json(const cJSON *item)
{
    canon_buf_t b = {0};
    canon_print(&b, item);
    if (b.err) {
        free(b.buf);
        return NULL;
    }
    if (!b.buf) {
        return strdup("");
    }
    return b.buf;
}

/* --------------- Public API --------------- */

void hm_protocol_init(hm_protocol_ctx_t *ctx, const char *password,
                       const char *site_id, hm_cipher_t preferred_cipher)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->password = password;
    ctx->site_id = site_id;
    ctx->preferred_cipher = preferred_cipher;
    ctx->preferred_encoding = HM_ENCODING_JSON_HEX;
    ctx->binarize = false;
    ctx->state = HM_STATE_CONNECTING;
    generate_uuid_v4(ctx->session_id);
    ESP_LOGI(TAG, "Protocol init, session_id=%s", ctx->session_id);
}

void hm_protocol_set_v3(hm_protocol_ctx_t *ctx,
                        const uint8_t psk[HM_NOISE_KEY_SIZE],
                        const uint8_t static_priv[HM_NOISE_KEY_SIZE],
                        const uint8_t *server_static_pub)
{
    memcpy(ctx->psk, psk, HM_NOISE_KEY_SIZE);
    memcpy(ctx->static_key, static_priv, HM_NOISE_KEY_SIZE);
    if (server_static_pub) {
        memcpy(ctx->server_static_key, server_static_pub, HM_NOISE_KEY_SIZE);
        ctx->has_server_static_key = true;
    }
    ctx->v3_enabled = true;
    ESP_LOGI(TAG, "Protocol v3 (Noise) enabled%s",
             server_static_pub ? ", server static key pinned" : "");
}

void hm_protocol_deinit(hm_protocol_ctx_t *ctx)
{
    free(ctx->server_hello_canon);
    ctx->server_hello_canon = NULL;
    free(ctx->pending_bin);
    ctx->pending_bin = NULL;
    ctx->pending_bin_len = 0;
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
    err = hm_crypto_encrypt_json(&ctx->crypto,
                                  (const uint8_t *)envelope, strlen(envelope),
                                  ctx->crypto.encoding, out, out_sz);
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

    return hm_crypto_encrypt_json(&ctx->crypto,
                                   (const uint8_t *)envelope, strlen(envelope),
                                   ctx->crypto.encoding, out, out_sz);
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
    esp_err_t err = hm_crypto_decrypt_json(&ctx->crypto, encrypted,
                                            ctx->crypto.encoding,
                                            pt_buf, sizeof(pt_buf) - 1, &pt_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Decryption failed");
        return err;
    }
    pt_buf[pt_len] = '\0';

    return hm_protocol_parse_envelope((const char *)pt_buf,
                                      type_out, payload_out, context_out);
}

esp_err_t hm_protocol_parse_envelope(const char *json,
                                     hm_msg_type_t *type_out,
                                     cJSON **payload_out,
                                     cJSON **context_out)
{
    esp_err_t err;
    cJSON *root = cJSON_Parse(json);
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

    /* Retain the exact canonical payload bytes for the Noise prologue
     * (HIVEMIND-CRYPTO-1 §3.4.3). */
    if (ctx->v3_enabled) {
        free(ctx->server_hello_canon);
        ctx->server_hello_canon = hm_protocol_canonical_json(payload);
    }

    ctx->state = HM_STATE_HELLO_RECEIVED;
    ESP_LOGI(TAG, "HELLO received, peer=%s node_id=%s",
             ctx->server_peer, ctx->server_node_id);
    return ESP_OK;
}

/* --------------- Protocol v3: Noise handshake --------------- */

/** True when the array contains the given string. */
static bool json_array_contains(const cJSON *arr, const char *value)
{
    const cJSON *item;
    cJSON_ArrayForEach(item, arr) {
        if (cJSON_IsString(item) && item->valuestring &&
            strcmp(item->valuestring, value) == 0) {
            return true;
        }
    }
    return false;
}

/** Hex-encode into a malloc'd string. */
static char *hex_dup(const uint8_t *data, size_t len)
{
    char *out = (char *)malloc(len * 2 + 1);
    if (!out) {
        return NULL;
    }
    for (size_t i = 0; i < len; i++) {
        sprintf(out + 2 * i, "%02x", data[i]);
    }
    return out;
}

/** Build a HANDSHAKE envelope carrying a Noise message
 *  ({"noise":{"pattern","suite","msg"}}) — pattern/suite only on message 1. */
static char *build_noise_envelope(const char *pattern, const char *suite,
                                  const uint8_t *msg, size_t msg_len)
{
    char *msg_hex = hex_dup(msg, msg_len);
    if (!msg_hex) {
        return NULL;
    }
    cJSON *payload = cJSON_CreateObject();
    if (!payload) {
        free(msg_hex);
        return NULL;
    }
    cJSON *noise = cJSON_AddObjectToObject(payload, "noise");
    if (pattern) {
        cJSON_AddStringToObject(noise, "pattern", pattern);
    }
    if (suite) {
        cJSON_AddStringToObject(noise, "suite", suite);
    }
    cJSON_AddStringToObject(noise, "msg", msg_hex);
    free(msg_hex);

    char envelope[2048];
    esp_err_t err = hm_protocol_build_envelope("shake", payload,
                                               envelope, sizeof(envelope));
    cJSON_Delete(payload);
    if (err != ESP_OK) {
        return NULL;
    }
    return strdup(envelope);
}

/**
 * @brief Decide whether the server's parameter HANDSHAKE offers a v3 Noise
 *        handshake this client can run; select the pattern.
 *
 * @return true and sets *pattern_out when the Noise path applies.
 */
static bool select_noise(const hm_protocol_ctx_t *ctx, const cJSON *payload,
                         hm_noise_pattern_t *pattern_out)
{
    if (!ctx->v3_enabled) {
        return false;
    }
    const cJSON *maxv = cJSON_GetObjectItemCaseSensitive(payload, "max_protocol_version");
    if (!cJSON_IsNumber(maxv) || maxv->valuedouble < 3) {
        return false;
    }
    const cJSON *noise = cJSON_GetObjectItemCaseSensitive(payload, "noise");
    if (!cJSON_IsObject(noise)) {
        return false;
    }
    const cJSON *suites = cJSON_GetObjectItemCaseSensitive(noise, "suites");
    if (!json_array_contains(suites, HM_NOISE_SUITE_CHACHA)) {
        ESP_LOGW(TAG, "Server does not offer the mandatory ChaChaPoly suite");
        return false;
    }
    const cJSON *patterns = cJSON_GetObjectItemCaseSensitive(noise, "patterns");
    /* KKpsk0 is preferred when the server static key is provisioned/pinned
     * and the server offers it (HIVEMIND-CRYPTO-1 §3.4.2). */
    if (ctx->has_server_static_key && json_array_contains(patterns, "KKpsk0")) {
        *pattern_out = HM_NOISE_PATTERN_KKPSK0;
        return true;
    }
    if (json_array_contains(patterns, "XXpsk2")) {
        *pattern_out = HM_NOISE_PATTERN_XXPSK2;
        return true;
    }
    return false;
}

/**
 * @brief Start the v3 Noise handshake: send Noise message 1 inside a
 *        HANDSHAKE envelope (HIVEMIND-CRYPTO-1 §3.4.3 step 3).
 */
static esp_err_t start_noise_handshake(hm_protocol_ctx_t *ctx,
                                       hm_noise_pattern_t pattern,
                                       const cJSON *shake_payload,
                                       char **reply_out)
{
    const char *pattern_name = hm_noise_pattern_name(pattern);
    char protocol_name[64];
    snprintf(protocol_name, sizeof(protocol_name), "Noise_%s_%s",
             pattern_name, HM_NOISE_SUITE_CHACHA);

    /* Prologue: canonical HELLO payload + canonical parameter HANDSHAKE
     * payload + selected protocol name (§3.4.3 downgrade protection). */
    char *shake_canon = hm_protocol_canonical_json(shake_payload);
    if (!shake_canon) {
        return ESP_ERR_NO_MEM;
    }
    const char *hello_canon = ctx->server_hello_canon ? ctx->server_hello_canon : "";
    size_t prologue_len = strlen(hello_canon) + strlen(shake_canon)
                          + strlen(protocol_name);
    uint8_t *prologue = (uint8_t *)malloc(prologue_len);
    if (!prologue) {
        free(shake_canon);
        return ESP_ERR_NO_MEM;
    }
    size_t off = 0;
    memcpy(prologue + off, hello_canon, strlen(hello_canon));
    off += strlen(hello_canon);
    memcpy(prologue + off, shake_canon, strlen(shake_canon));
    off += strlen(shake_canon);
    memcpy(prologue + off, protocol_name, strlen(protocol_name));
    free(shake_canon);

    esp_err_t err = hm_noise_init(&ctx->noise, pattern, ctx->psk,
                                  ctx->static_key,
                                  ctx->has_server_static_key ? ctx->server_static_key : NULL,
                                  prologue, prologue_len, NULL);
    free(prologue);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Noise init failed");
        return err;
    }

    /* Noise message 1 payload: our encodings + binarize capability. */
    cJSON *msg_payload = cJSON_CreateObject();
    if (!msg_payload) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddBoolToObject(msg_payload, "binarize", ctx->binarize);
    cJSON *encodings = cJSON_AddArrayToObject(msg_payload, "encodings");
    cJSON_AddItemToArray(encodings,
                         cJSON_CreateString(hm_encoding_name(ctx->preferred_encoding)));
    char *payload_json = hm_protocol_canonical_json(msg_payload);
    cJSON_Delete(msg_payload);
    if (!payload_json) {
        return ESP_ERR_NO_MEM;
    }

    uint8_t msg1[256];
    size_t msg1_len = 0;
    err = hm_noise_write_message(&ctx->noise,
                                 (const uint8_t *)payload_json, strlen(payload_json),
                                 msg1, sizeof(msg1), &msg1_len);
    free(payload_json);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write Noise message 1");
        return err;
    }

    *reply_out = build_noise_envelope(pattern_name, HM_NOISE_SUITE_CHACHA,
                                      msg1, msg1_len);
    if (!*reply_out) {
        return ESP_ERR_NO_MEM;
    }

    ctx->state = HM_STATE_NOISE_HANDSHAKE_SENT;
    ESP_LOGI(TAG, "Noise message 1 sent (%s), state=NOISE_HANDSHAKE_SENT",
             protocol_name);
    return ESP_OK;
}

/**
 * @brief Handle the server's Noise message 2 (HIVEMIND-CRYPTO-1 §3.4.3
 *        steps 4-7): complete the handshake, TOFU-pin the server static key,
 *        send Noise message 3 (XXpsk2), and queue the encrypted HELLO as the
 *        first Noise transport message.
 */
static esp_err_t handle_noise_shake_response(hm_protocol_ctx_t *ctx,
                                             const cJSON *payload,
                                             char **reply_out)
{
    const cJSON *noise = cJSON_GetObjectItemCaseSensitive(payload, "noise");
    const char *msg_hex = NULL;
    if (cJSON_IsObject(noise)) {
        const cJSON *msg_item = cJSON_GetObjectItemCaseSensitive(noise, "msg");
        if (cJSON_IsString(msg_item)) {
            msg_hex = msg_item->valuestring;
        }
    }
    if (!msg_hex) {
        ESP_LOGE(TAG, "Malformed Noise HANDSHAKE envelope");
        return ESP_ERR_INVALID_RESPONSE;
    }

    size_t hex_len = strlen(msg_hex);
    if (hex_len % 2 != 0 || hex_len > 2 * 512) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    uint8_t msg[512];
    size_t msg_len = hex_len / 2;
    for (size_t i = 0; i < msg_len; i++) {
        unsigned int v;
        if (sscanf(msg_hex + 2 * i, "%2x", &v) != 1) {
            return ESP_ERR_INVALID_RESPONSE;
        }
        msg[i] = (uint8_t)v;
    }

    uint8_t noise_payload[256];
    size_t noise_payload_len = 0;
    esp_err_t err = hm_noise_read_message(&ctx->noise, msg, msg_len,
                                          noise_payload, sizeof(noise_payload) - 1,
                                          &noise_payload_len);
    if (err != ESP_OK) {
        /* Wrong PSK / tampered negotiation / wrong static key — fatal. */
        ESP_LOGE(TAG, "Noise handshake FAILED — rejecting connection");
        return err;
    }

    /* TOFU-then-pin the server static key (§3.4.5). */
    if (ctx->noise.has_rs) {
        if (ctx->has_server_static_key &&
            memcmp(ctx->noise.rs, ctx->server_static_key, HM_NOISE_KEY_SIZE) != 0) {
            ESP_LOGE(TAG, "Server Noise static key CHANGED — possible "
                          "man-in-the-middle, aborting");
            return ESP_ERR_INVALID_STATE;
        }
        if (!ctx->has_server_static_key) {
            memcpy(ctx->server_static_key, ctx->noise.rs, HM_NOISE_KEY_SIZE);
            ctx->has_server_static_key = true;
        }
    }

    /* Server's (encrypted) payload carries its selected encoding. */
    noise_payload[noise_payload_len] = '\0';
    cJSON *selection = cJSON_Parse((const char *)noise_payload);
    if (selection) {
        const char *enc = json_get_string(selection, "encoding");
        hm_encoding_t server_enc;
        if (enc && hm_encoding_parse(enc, &server_enc) == ESP_OK) {
            ctx->crypto.encoding = server_enc;
        }
        cJSON_Delete(selection);
    }

    /* XXpsk2 message 3: s, se (empty payload). */
    if (!ctx->noise.finished) {
        uint8_t msg3[128];
        size_t msg3_len = 0;
        err = hm_noise_write_message(&ctx->noise, NULL, 0,
                                     msg3, sizeof(msg3), &msg3_len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to write Noise message 3");
            return err;
        }
        *reply_out = build_noise_envelope(NULL, NULL, msg3, msg3_len);
        if (!*reply_out) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (!ctx->noise.finished) {
        ESP_LOGE(TAG, "Noise handshake did not complete");
        return ESP_ERR_INVALID_STATE;
    }

    /* First Noise transport message: the encrypted HELLO (§3.4.3 step 7). */
    cJSON *hello = cJSON_CreateObject();
    if (!hello) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(hello, "pubkey", "");
    cJSON *session = cJSON_AddObjectToObject(hello, "session");
    cJSON_AddStringToObject(session, "session_id", ctx->session_id);
    cJSON_AddStringToObject(hello, "site_id", ctx->site_id ? ctx->site_id : "unknown");

    char envelope[2048];
    err = hm_protocol_build_envelope("hello", hello, envelope, sizeof(envelope));
    cJSON_Delete(hello);
    if (err != ESP_OK) {
        return err;
    }

    ctx->use_noise = true; /* transport CipherStates take over from here */
    size_t env_len = strlen(envelope);
    uint8_t *frame = (uint8_t *)malloc(env_len + 1 + HM_NOISE_TAG_SIZE);
    if (!frame) {
        return ESP_ERR_NO_MEM;
    }
    size_t frame_len = 0;
    err = hm_protocol_noise_encrypt_frame(ctx, (const uint8_t *)envelope, env_len,
                                          false, frame,
                                          env_len + 1 + HM_NOISE_TAG_SIZE, &frame_len);
    if (err != ESP_OK) {
        free(frame);
        return err;
    }
    free(ctx->pending_bin);
    ctx->pending_bin = frame;
    ctx->pending_bin_len = frame_len;

    ctx->state = HM_STATE_READY;
    ESP_LOGI(TAG, "Protocol v3 Noise session established, state=READY");
    return ESP_OK;
}

esp_err_t hm_protocol_noise_encrypt_frame(hm_protocol_ctx_t *ctx,
                                          const uint8_t *payload, size_t len,
                                          bool is_binary,
                                          uint8_t *out, size_t out_sz,
                                          size_t *out_len)
{
    if (!ctx->use_noise) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t *pt = (uint8_t *)malloc(len + 1);
    if (!pt) {
        return ESP_ERR_NO_MEM;
    }
    pt[0] = is_binary ? 0x01 : 0x00; /* v3 frame marker */
    memcpy(pt + 1, payload, len);
    esp_err_t err = hm_noise_encrypt(&ctx->noise, pt, len + 1,
                                     out, out_sz, out_len);
    free(pt);
    return err;
}

esp_err_t hm_protocol_noise_decrypt_frame(hm_protocol_ctx_t *ctx,
                                          const uint8_t *frame, size_t frame_len,
                                          uint8_t *out, size_t out_sz,
                                          size_t *out_len, bool *is_binary)
{
    if (!ctx->use_noise) {
        return ESP_ERR_INVALID_STATE;
    }
    if (frame_len < 1 + HM_NOISE_TAG_SIZE) {
        return ESP_ERR_INVALID_SIZE;
    }
    uint8_t *pt = (uint8_t *)malloc(frame_len);
    if (!pt) {
        return ESP_ERR_NO_MEM;
    }
    size_t pt_len = 0;
    esp_err_t err = hm_noise_decrypt(&ctx->noise, frame, frame_len,
                                     pt, frame_len, &pt_len);
    if (err != ESP_OK || pt_len < 1) {
        free(pt);
        return (err != ESP_OK) ? err : ESP_ERR_INVALID_RESPONSE;
    }
    if (pt[0] != 0x00 && pt[0] != 0x01) {
        ESP_LOGE(TAG, "Unknown v3 frame marker: 0x%02x", pt[0]);
        free(pt);
        return ESP_ERR_INVALID_RESPONSE;
    }
    *is_binary = (pt[0] == 0x01);
    if (pt_len - 1 > out_sz) {
        free(pt);
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(out, pt + 1, pt_len - 1);
    *out_len = pt_len - 1;
    free(pt);
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

    /* Protocol v3 (HIVEMIND-WIRE-1 §2): when the server advertises v3 with
     * Noise parameters and a PSK is provisioned, run the Noise handshake;
     * otherwise fall through to the legacy (v0-v2) hsub handshake. */
    hm_noise_pattern_t pattern;
    if (select_noise(ctx, payload, &pattern)) {
        return start_noise_handshake(ctx, pattern, payload, reply_out);
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
    cJSON_AddItemToArray(encodings, cJSON_CreateString(hm_encoding_name(ctx->preferred_encoding)));

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

    /* Set negotiated encoding */
    const cJSON *enc_item = cJSON_GetObjectItemCaseSensitive(payload, "encoding");
    if (cJSON_IsString(enc_item) && enc_item->valuestring) {
        hm_encoding_t server_enc;
        if (hm_encoding_parse(enc_item->valuestring, &server_enc) == ESP_OK) {
            ctx->crypto.encoding = server_enc;
        } else {
            ctx->crypto.encoding = ctx->preferred_encoding;
        }
    } else {
        ctx->crypto.encoding = ctx->preferred_encoding;
    }

    ctx->state = HM_STATE_KEY_DERIVED;
    ESP_LOGI(TAG, "Key derived, cipher=%s, encoding=%s",
             hm_crypto_cipher_name(ctx->crypto.cipher),
             hm_encoding_name(ctx->crypto.encoding));

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

    case HM_STATE_NOISE_HANDSHAKE_SENT:
        if (msg_type != HM_MSG_HANDSHAKE) {
            ESP_LOGE(TAG, "Expected SHAKE in NOISE_HANDSHAKE_SENT state, got %s", type_str);
            err = ESP_ERR_INVALID_STATE;
        } else {
            err = handle_noise_shake_response(ctx, payload ? payload : root, reply_out);
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
