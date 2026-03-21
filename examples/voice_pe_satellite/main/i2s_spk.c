/**
 * @file i2s_spk.c
 * @brief I2S speaker output to AIC3204 DAC with upsampling and ring buffer.
 *
 * TTS audio arrives as 16-bit mono PCM at 16 kHz from the HiveMind hub.
 * The playback task reads from a FreeRTOS ring buffer, upsamples to 48 kHz
 * by triplicating each sample, zero-extends to 32-bit, and duplicates to
 * stereo for the AIC3204 DAC.
 */
#include "i2s_spk.h"
#include "voice_pe_hw.h"

#include "driver/i2s.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"

#include <stdbool.h>
#include <string.h>

static const char *TAG = "i2s_spk";

/* 32 KB ring buffer for incoming TTS PCM chunks. */
#define SPK_RINGBUF_SIZE (32 * 1024)

static RingbufHandle_t s_ringbuf = NULL;

esp_err_t i2s_spk_init(void)
{
    /* Enable speaker amplifier. */
    gpio_config_t amp_cfg = {
        .pin_bit_mask = 1ULL << VP_SPK_AMP_EN,
        .mode         = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&amp_cfg));
    gpio_set_level(VP_SPK_AMP_EN, 1);

    i2s_config_t cfg = {
        .mode                 = I2S_MODE_MASTER | I2S_MODE_TX,
        .sample_rate          = VP_SPK_SAMPLE_RATE,
        .bits_per_sample      = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count        = VP_SPK_DMA_BUF_COUNT,
        .dma_buf_len          = VP_SPK_DMA_BUF_LEN,
        .use_apll             = false,
    };

    i2s_pin_config_t pins = {
        .bck_io_num   = VP_SPK_BCLK,
        .ws_io_num    = VP_SPK_LRCLK,
        .data_out_num = VP_SPK_DOUT,
        .data_in_num  = I2S_PIN_NO_CHANGE,
    };

    ESP_ERROR_CHECK(i2s_driver_install(VP_SPK_I2S_PORT, &cfg, 0, NULL));
    ESP_ERROR_CHECK(i2s_set_pin(VP_SPK_I2S_PORT, &pins));

    /* Create ring buffer in PSRAM if available. */
    s_ringbuf = xRingbufferCreate(SPK_RINGBUF_SIZE, RINGBUF_TYPE_BYTEBUF);
    if (s_ringbuf == NULL) {
        ESP_LOGE(TAG, "Failed to create playback ring buffer");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "I2S speaker initialized: %d Hz, 32-bit stereo on port %d",
             VP_SPK_SAMPLE_RATE, VP_SPK_I2S_PORT);
    return ESP_OK;
}

esp_err_t i2s_spk_push(const int16_t *pcm16k_mono, size_t samples)
{
    if (s_ringbuf == NULL || samples == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    BaseType_t ret = xRingbufferSend(s_ringbuf, pcm16k_mono,
                                      samples * sizeof(int16_t),
                                      pdMS_TO_TICKS(100));
    return (ret == pdTRUE) ? ESP_OK : ESP_ERR_TIMEOUT;
}

bool i2s_spk_is_idle(void)
{
    if (s_ringbuf == NULL) {
        return true;
    }
    UBaseType_t free_size = xRingbufferGetCurFreeSize(s_ringbuf);
    return (free_size >= SPK_RINGBUF_SIZE - 64);
}

/**
 * Playback task: read 16-bit mono PCM at 16 kHz from ring buffer,
 * upsample 3× to 48 kHz, expand to 32-bit stereo, write to I2S.
 */
static void playback_task(void *arg)
{
    /* Process 128 input samples at a time.
     * 128 samples × 3 (upsample) = 384 output frames.
     * 384 frames × 2 channels × 4 bytes = 3072 bytes per I2S write. */
    const size_t CHUNK = 128;
    int32_t out_buf[CHUNK * 3 * 2];  /* 384 stereo frames */

    ESP_LOGI(TAG, "Playback task started");

    while (1) {
        size_t item_size = 0;
        int16_t *data = (int16_t *)xRingbufferReceiveUpTo(
            s_ringbuf, &item_size, pdMS_TO_TICKS(50), CHUNK * sizeof(int16_t));

        if (data == NULL || item_size == 0) {
            /* No data — write silence to keep I2S clock running. */
            memset(out_buf, 0, sizeof(out_buf));
            size_t written = 0;
            i2s_write(VP_SPK_I2S_PORT, out_buf, 256, &written, pdMS_TO_TICKS(10));
            continue;
        }

        size_t in_samples = item_size / sizeof(int16_t);
        size_t out_idx = 0;

        /* Upsample 16 kHz → 48 kHz by triplicating each sample.
         * Expand 16-bit → 32-bit by left-shifting. Duplicate to stereo. */
        for (size_t i = 0; i < in_samples; i++) {
            int32_t sample32 = ((int32_t)data[i]) << 16;
            for (int r = 0; r < 3; r++) {
                out_buf[out_idx++] = sample32;  /* Left */
                out_buf[out_idx++] = sample32;  /* Right */
            }
        }

        vRingbufferReturnItem(s_ringbuf, data);

        size_t bytes = out_idx * sizeof(int32_t);
        size_t written = 0;
        i2s_write(VP_SPK_I2S_PORT, out_buf, bytes, &written, portMAX_DELAY);
    }
}

void i2s_spk_start_task(void)
{
    xTaskCreate(playback_task, "spk_play", 4096, NULL, 5, NULL);
}
