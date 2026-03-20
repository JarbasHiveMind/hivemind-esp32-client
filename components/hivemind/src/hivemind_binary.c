/**
 * @file hivemind_binary.c
 * @brief HiveMind bitstring binary protocol V1 codec.
 */

#include "hivemind_binary.h"
#include <string.h>
#include "esp_log.h"

static const char *TAG = "hm_binary";

/* ── BitWriter ─────────────────────────────────────────────────────── */

typedef struct {
    uint8_t *buf;
    size_t   cap;
    size_t   bit_pos;
} bit_writer_t;

static void bw_init(bit_writer_t *w, uint8_t *buf, size_t cap) {
    w->buf = buf;
    w->cap = cap;
    w->bit_pos = 0;
    memset(buf, 0, cap);
}

static void bw_write_bits(bit_writer_t *w, uint32_t val, int n) {
    for (int i = n - 1; i >= 0; i--) {
        size_t byte_idx = w->bit_pos / 8;
        int bit_idx = 7 - (w->bit_pos % 8);
        if (byte_idx < w->cap) {
            w->buf[byte_idx] |= (uint8_t)(((val >> i) & 1) << bit_idx);
        }
        w->bit_pos++;
    }
}

static void bw_write_bytes(bit_writer_t *w, const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        bw_write_bits(w, data[i], 8);
    }
}

/* ── BitReader ─────────────────────────────────────────────────────── */

typedef struct {
    const uint8_t *buf;
    size_t         len;
    size_t         bit_pos;
} bit_reader_t;

static void br_init(bit_reader_t *r, const uint8_t *buf, size_t len) {
    r->buf = buf;
    r->len = len;
    r->bit_pos = 0;
}

static uint32_t br_read_bits(bit_reader_t *r, int n) {
    uint32_t val = 0;
    for (int i = 0; i < n; i++) {
        size_t byte_idx = r->bit_pos / 8;
        int bit_idx = 7 - (r->bit_pos % 8);
        if (byte_idx < r->len) {
            val = (val << 1) | ((r->buf[byte_idx] >> bit_idx) & 1);
        }
        r->bit_pos++;
    }
    return val;
}

static const uint8_t *br_read_bytes(bit_reader_t *r, size_t n) {
    /* Must be byte-aligned. */
    if (r->bit_pos % 8 != 0) {
        return NULL;
    }
    size_t byte_off = r->bit_pos / 8;
    if (byte_off + n > r->len) {
        return NULL;
    }
    const uint8_t *ptr = r->buf + byte_off;
    r->bit_pos += n * 8;
    return ptr;
}

/* ── Encode ────────────────────────────────────────────────────────── */

esp_err_t hm_binary_encode(hm_msg_type_t msg_type, hm_bin_type_t bin_type,
                            const uint8_t *metadata, size_t meta_len,
                            const uint8_t *payload, size_t payload_len,
                            uint8_t *out, size_t out_sz, size_t *out_len) {
    if (meta_len > 255) {
        ESP_LOGE(TAG, "metadata too long: %zu", meta_len);
        return ESP_ERR_INVALID_ARG;
    }

    /* Calculate content bits (after pad marker). */
    int content_bits = 1                        /* versioned */
                     + 8                        /* protocol_version (always versioned) */
                     + 5                        /* msg_type */
                     + 1                        /* compressed */
                     + 8                        /* meta_length */
                     + (int)(meta_len * 8)      /* metadata */
                     + (int)(payload_len * 8);  /* payload */
    if (msg_type == HM_MSG_BINARY) {
        content_bits += 4;                      /* bin_type */
    }

    /* Total bits = pad_zeros + 1 (marker) + content_bits, rounded up to byte boundary. */
    int total_content = 1 + content_bits;       /* marker + content */
    int total_bytes = (total_content + 7) / 8;
    int total_bits = total_bytes * 8;
    int pad_zeros = total_bits - total_content;

    if ((size_t)total_bytes > out_sz) {
        ESP_LOGE(TAG, "output buffer too small: need %d, have %zu", total_bytes, out_sz);
        return ESP_ERR_NO_MEM;
    }

    bit_writer_t w;
    bw_init(&w, out, out_sz);

    /* Leading 0-bit padding. */
    bw_write_bits(&w, 0, pad_zeros);
    /* Pad marker. */
    bw_write_bits(&w, 1, 1);
    /* Versioned = true. */
    bw_write_bits(&w, 1, 1);
    /* Protocol version = 1. */
    bw_write_bits(&w, 1, 8);
    /* Message type. */
    bw_write_bits(&w, (uint32_t)msg_type, 5);
    /* Compressed = false. */
    bw_write_bits(&w, 0, 1);
    /* Metadata length. */
    bw_write_bits(&w, (uint32_t)meta_len, 8);
    /* Metadata. */
    if (meta_len > 0 && metadata) {
        bw_write_bytes(&w, metadata, meta_len);
    }
    /* Binary type (only for BINARY messages). */
    if (msg_type == HM_MSG_BINARY) {
        bw_write_bits(&w, (uint32_t)bin_type, 4);
    }
    /* Payload. */
    if (payload_len > 0 && payload) {
        bw_write_bytes(&w, payload, payload_len);
    }

    *out_len = (size_t)total_bytes;
    return ESP_OK;
}

/* ── Decode ────────────────────────────────────────────────────────── */

esp_err_t hm_binary_decode(const uint8_t *data, size_t data_len,
                            hm_binary_frame_t *frame_out) {
    if (!data || data_len == 0 || !frame_out) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(frame_out, 0, sizeof(*frame_out));

    bit_reader_t r;
    br_init(&r, data, data_len);

    /* Skip leading 0s, find pad marker (first 1 bit). */
    size_t total_bits = data_len * 8;
    while (r.bit_pos < total_bits && br_read_bits(&r, 1) == 0) {
        /* skip */
    }
    /* The 1-bit pad marker was just consumed by the loop iteration that returned 1. */

    if (r.bit_pos >= total_bits) {
        ESP_LOGE(TAG, "no pad marker found");
        return ESP_ERR_INVALID_RESPONSE;
    }

    /* Versioned flag. */
    frame_out->versioned = (bool)br_read_bits(&r, 1);
    if (frame_out->versioned) {
        frame_out->protocol_version = (uint8_t)br_read_bits(&r, 8);
    }

    frame_out->msg_type = (hm_msg_type_t)br_read_bits(&r, 5);
    frame_out->compressed = (bool)br_read_bits(&r, 1);

    uint32_t meta_len = br_read_bits(&r, 8);
    frame_out->metadata_len = (size_t)meta_len;

    if (meta_len > 0) {
        frame_out->metadata = br_read_bytes(&r, meta_len);
        if (!frame_out->metadata) {
            ESP_LOGE(TAG, "failed to read metadata");
            return ESP_ERR_INVALID_SIZE;
        }
    }

    if (frame_out->msg_type == HM_MSG_BINARY) {
        frame_out->bin_type = (hm_bin_type_t)br_read_bits(&r, 4);
    }

    /* Payload: remaining whole bytes. */
    if (r.bit_pos % 8 != 0) {
        ESP_LOGE(TAG, "payload not byte-aligned (bit_pos=%zu)", r.bit_pos);
        return ESP_ERR_INVALID_STATE;
    }
    size_t byte_off = r.bit_pos / 8;
    if (byte_off < data_len) {
        frame_out->payload = data + byte_off;
        frame_out->payload_len = data_len - byte_off;
    }

    return ESP_OK;
}

/* ── String helpers ────────────────────────────────────────────────── */

static const char *const s_type_names[] = {
    [HM_MSG_HANDSHAKE]  = "HANDSHAKE",
    [HM_MSG_BUS]        = "BUS",
    [HM_MSG_SHARED_BUS] = "SHARED_BUS",
    [HM_MSG_BROADCAST]  = "BROADCAST",
    [HM_MSG_PROPAGATE]  = "PROPAGATE",
    [HM_MSG_ESCALATE]   = "ESCALATE",
    [HM_MSG_HELLO]      = "HELLO",
    [HM_MSG_QUERY]      = "QUERY",
    [HM_MSG_CASCADE]    = "CASCADE",
    [HM_MSG_PING]       = "PING",
    [HM_MSG_RENDEZVOUS] = "RENDEZVOUS",
    [HM_MSG_THIRDPARTY] = "THIRDPARTY",
    [HM_MSG_BINARY]     = "BINARY",
    [HM_MSG_PONG]       = "PONG",
};

#define NUM_MSG_TYPES (sizeof(s_type_names) / sizeof(s_type_names[0]))

const char *hm_msg_type_str(hm_msg_type_t type) {
    if ((unsigned)type < NUM_MSG_TYPES && s_type_names[type]) {
        return s_type_names[type];
    }
    return "UNKNOWN";
}

esp_err_t hm_msg_type_parse(const char *name, hm_msg_type_t *type_out) {
    if (!name || !type_out) {
        return ESP_ERR_INVALID_ARG;
    }
    for (unsigned i = 0; i < NUM_MSG_TYPES; i++) {
        if (s_type_names[i] && strcasecmp(name, s_type_names[i]) == 0) {
            *type_out = (hm_msg_type_t)i;
            return ESP_OK;
        }
    }
    ESP_LOGE(TAG, "unknown message type: %s", name);
    return ESP_ERR_NOT_FOUND;
}
