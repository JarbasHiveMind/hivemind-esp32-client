/**
 * @file i2s_mic.c
 * @brief I2S microphone input from XMOS Voice Kit.
 *
 * XMOS outputs 32-bit stereo I2S at 16 kHz. We read both channels,
 * extract the left channel (channel 0 = processed audio with AEC/NS/AGC),
 * and down-shift to 16-bit for HiveMind binary audio frames.
 */
#include "i2s_mic.h"
#include "voice_pe_hw.h"

#include "driver/i2s.h"
#include "esp_log.h"

static const char *TAG = "i2s_mic";

esp_err_t i2s_mic_init(void)
{
    i2s_config_t cfg = {
        .mode                 = I2S_MODE_MASTER | I2S_MODE_RX,
        .sample_rate          = VP_MIC_SAMPLE_RATE,
        .bits_per_sample      = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count        = VP_MIC_DMA_BUF_COUNT,
        .dma_buf_len          = VP_MIC_DMA_BUF_LEN,
        .use_apll             = false,
    };

    i2s_pin_config_t pins = {
        .bck_io_num   = VP_MIC_BCLK,
        .ws_io_num    = VP_MIC_LRCLK,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num  = VP_MIC_DIN,
    };

    ESP_ERROR_CHECK(i2s_driver_install(VP_MIC_I2S_PORT, &cfg, 0, NULL));
    ESP_ERROR_CHECK(i2s_set_pin(VP_MIC_I2S_PORT, &pins));

    ESP_LOGI(TAG, "I2S mic initialized: %d Hz, 32-bit stereo on port %d",
             VP_MIC_SAMPLE_RATE, VP_MIC_I2S_PORT);
    return ESP_OK;
}

esp_err_t i2s_mic_read(int16_t *buf, size_t max_samples, size_t *samples_read)
{
    /* Read 32-bit stereo: each frame = 2 × 32-bit = 8 bytes.
     * We need max_samples frames → max_samples × 8 bytes. */
    size_t raw_bytes = max_samples * 8;
    if (raw_bytes > 4096) {
        raw_bytes = 4096;
    }
    int32_t raw[512];  /* 512 × 4 = 2048 bytes → 256 stereo frames */
    if (raw_bytes > sizeof(raw)) {
        raw_bytes = sizeof(raw);
    }

    size_t bytes_read = 0;
    esp_err_t ret = i2s_read(VP_MIC_I2S_PORT, raw, raw_bytes,
                              &bytes_read, portMAX_DELAY);
    if (ret != ESP_OK) {
        *samples_read = 0;
        return ret;
    }

    /* Extract left channel (even indices) and shift 32-bit → 16-bit. */
    size_t stereo_samples = bytes_read / 4;  /* total 32-bit words */
    size_t mono_count = 0;
    for (size_t i = 0; i < stereo_samples; i += 2) {
        if (mono_count >= max_samples) {
            break;
        }
        buf[mono_count++] = (int16_t)(raw[i] >> 16);
    }

    *samples_read = mono_count;
    return ESP_OK;
}
