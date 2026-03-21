/**
 * @file codec_init.c
 * @brief I2C bus init, XMOS Voice Kit reset/probe, TI AIC3204 DAC register config.
 *
 * The XMOS codec runs its own firmware (assume pre-flashed via USB DFU).
 * We only reset it and verify I2C presence.
 *
 * The AIC3204 requires an I2C register init sequence to configure the DAC
 * for 48 kHz stereo output from I2S BCLK source.
 */
#include "codec_init.h"
#include "voice_pe_hw.h"

#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "codec";

/* ── I2C helpers ─────────────────────────────────────────────────── */

static esp_err_t i2c_bus_init(void)
{
    i2c_config_t conf = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = VP_I2C_SDA,
        .scl_io_num       = VP_I2C_SCL,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = VP_I2C_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_param_config(VP_I2C_PORT, &conf));
    return i2c_driver_install(VP_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
}

static esp_err_t i2c_write_reg(uint8_t dev_addr, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_write_to_device(VP_I2C_PORT, dev_addr, buf, 2,
                                       pdMS_TO_TICKS(100));
}

static esp_err_t i2c_probe(uint8_t dev_addr)
{
    /* Send address byte only, check for ACK. */
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (dev_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(VP_I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return ret;
}

/* ── XMOS Voice Kit ──────────────────────────────────────────────── */

static esp_err_t xmos_init(void)
{
    /* Reset pulse: drive low 10 ms, release high, wait 500 ms for boot. */
    gpio_config_t rst_cfg = {
        .pin_bit_mask = 1ULL << VP_XMOS_RESET_PIN,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&rst_cfg));

    gpio_set_level(VP_XMOS_RESET_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(VP_XMOS_RESET_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(500));

    /* Probe I2C to verify presence. */
    esp_err_t ret = i2c_probe(VP_XMOS_I2C_ADDR);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "XMOS codec detected at 0x%02X", VP_XMOS_I2C_ADDR);
    } else {
        ESP_LOGW(TAG, "XMOS codec NOT detected at 0x%02X (err=%d). "
                 "Mic input may not work.", VP_XMOS_I2C_ADDR, ret);
    }
    return ESP_OK;  /* Non-fatal — XMOS may still output audio on I2S. */
}

/* ── TI AIC3204 DAC ──────────────────────────────────────────────── */

/**
 * AIC3204 register init sequence for 48 kHz stereo DAC output.
 * Clock source: I2S BCLK. Reference: TI AIC3204 Application Reference Guide.
 *
 * The exact sequence assumes BCLK = 48000 * 32 * 2 = 3.072 MHz from the ESP32
 * I2S master. PLL multiplies BCLK to generate the internal codec MCLK.
 */
static esp_err_t aic3204_init(void)
{
    esp_err_t ret = i2c_probe(VP_AIC3204_I2C_ADDR);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "AIC3204 NOT detected at 0x%02X", VP_AIC3204_I2C_ADDR);
        return ret;
    }
    ESP_LOGI(TAG, "AIC3204 detected at 0x%02X", VP_AIC3204_I2C_ADDR);

    const uint8_t addr = VP_AIC3204_I2C_ADDR;

    /* Page 0 — Clock and interface configuration */
    i2c_write_reg(addr, 0x00, 0x00);  /* Select page 0 */
    i2c_write_reg(addr, 0x01, 0x01);  /* Software reset */
    vTaskDelay(pdMS_TO_TICKS(10));

    /* Clock source: BCLK. PLL enabled to derive CODEC_CLKIN. */
    i2c_write_reg(addr, 0x00, 0x00);  /* Page 0 */
    i2c_write_reg(addr, 0x04, 0x07);  /* PLL_CLKIN = BCLK, CODEC_CLKIN = PLL */
    i2c_write_reg(addr, 0x05, 0x91);  /* PLL power up, P=1, R=1 */
    i2c_write_reg(addr, 0x06, 0x20);  /* PLL J = 32 */
    i2c_write_reg(addr, 0x07, 0x00);  /* PLL D = 0 (MSB) */
    i2c_write_reg(addr, 0x08, 0x00);  /* PLL D = 0 (LSB) */

    /* DAC clock dividers for 48 kHz: NDAC=2, MDAC=8, DOSR=128 */
    i2c_write_reg(addr, 0x0B, 0x82);  /* NDAC = 2, power up */
    i2c_write_reg(addr, 0x0C, 0x88);  /* MDAC = 8, power up */
    i2c_write_reg(addr, 0x0D, 0x00);  /* DOSR MSB = 0 */
    i2c_write_reg(addr, 0x0E, 0x80);  /* DOSR LSB = 128 */

    /* Audio interface: I2S, 32-bit, slave mode (ESP32 is master) */
    i2c_write_reg(addr, 0x1B, 0x30);  /* I2S, 32-bit word length */

    /* DAC setup */
    i2c_write_reg(addr, 0x3F, 0xD4);  /* DAC power: L+R on, data path L=LEFT R=RIGHT */
    i2c_write_reg(addr, 0x40, 0x00);  /* DAC unmute */
    i2c_write_reg(addr, 0x41, 0x00);  /* DAC L volume = 0 dB */
    i2c_write_reg(addr, 0x42, 0x00);  /* DAC R volume = 0 dB */

    /* Page 1 — Output routing and power */
    i2c_write_reg(addr, 0x00, 0x01);  /* Select page 1 */
    i2c_write_reg(addr, 0x01, 0x08);  /* Disable weak connection of AVDD to HP */
    i2c_write_reg(addr, 0x02, 0x01);  /* Enable master analog power control */
    i2c_write_reg(addr, 0x0A, 0x00);  /* Full power output driver */

    /* Route DAC_L to HPL, DAC_R to HPR */
    i2c_write_reg(addr, 0x0C, 0x08);  /* DAC_L routed to HPL */
    i2c_write_reg(addr, 0x0D, 0x08);  /* DAC_R routed to HPR */

    /* HP driver gain and power up */
    i2c_write_reg(addr, 0x10, 0x00);  /* HPL gain = 0 dB */
    i2c_write_reg(addr, 0x11, 0x00);  /* HPR gain = 0 dB */
    i2c_write_reg(addr, 0x09, 0x3C);  /* Power up HPL + HPR drivers */
    vTaskDelay(pdMS_TO_TICKS(50));     /* Wait for power stabilization */

    /* Back to page 0 for normal operation */
    i2c_write_reg(addr, 0x00, 0x00);

    ESP_LOGI(TAG, "AIC3204 configured for 48 kHz stereo DAC output");
    return ESP_OK;
}

/* ── Public API ──────────────────────────────────────────────────── */

esp_err_t codec_init_all(void)
{
    ESP_LOGI(TAG, "Initializing I2C bus");
    ESP_ERROR_CHECK(i2c_bus_init());

    ESP_LOGI(TAG, "Initializing XMOS Voice Kit");
    ESP_ERROR_CHECK(xmos_init());

    ESP_LOGI(TAG, "Initializing AIC3204 DAC");
    esp_err_t ret = aic3204_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "AIC3204 init failed — speaker output will not work");
    }
    return ret;
}
