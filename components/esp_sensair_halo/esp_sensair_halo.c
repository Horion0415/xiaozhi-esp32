/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <sys/ioctl.h>
#include "bsp/esp_sensair_halo.h"
#include "bsp_err_check.h"
#include "esp_log.h"
#include "esp_check.h"

#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "driver/i2s_pdm.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_io_parl.h"
#include "esp_lcd_touch_cst816s.h"
#include "esp_codec_dev_defaults.h"
#include "adc_mic.h"
#include "i2c_bus.h"
#include "common/common.h"
#include "soc/gpio_sig_map.h"
#include "soc/io_mux_reg.h"

#if __has_include("esp_video_init.h")
#include "linux/videodev2.h"
#include "esp_video_init.h"
#include "esp_video_device.h"
#define BSP_CAMERA_SUPPORTED 1
#else
#define BSP_CAMERA_SUPPORTED 0
#endif

static const char *TAG = "BSP_SENSAIRHALO";

static i2c_master_bus_handle_t s_i2c_handle = NULL;
static led_strip_handle_t s_led_strip = NULL;
static i2s_chan_handle_t s_speaker_tx = NULL;
static esp_codec_dev_handle_t s_speaker_codec = NULL;
static esp_codec_dev_handle_t s_mic_codec = NULL;
static i2c_bus_handle_t s_imu_i2c_bus = NULL;
static bmi270_handle_t s_bmi_handle = NULL;
static bmm150_aux_handle_t s_bmm_handle;
static bool s_bmm_initialized = false;
#if BSP_CAMERA_SUPPORTED
static bool s_camera_initialized = false;
#endif

/* ━━━━━━━━━━━━━━ ILI9341 Vendor Init Commands ━━━━━━━━━━━━━━ */

static const ili9341_lcd_init_cmd_t s_vendor_init_cmds[] = {
    {0x11, NULL, 0, 120},
    {0x36, (uint8_t []){0x00}, 1, 0},
    {0x3A, (uint8_t []){0x05}, 1, 0},
    {0xB2, (uint8_t []){0x0C, 0x0C, 0x00, 0x33, 0x33}, 5, 0},
    {0xB7, (uint8_t []){0x05}, 1, 0},
    {0xBB, (uint8_t []){0x21}, 1, 0},
    {0xC0, (uint8_t []){0x2C}, 1, 0},
    {0xC2, (uint8_t []){0x01}, 1, 0},
    {0xC3, (uint8_t []){0x15}, 1, 0},
    {0xC6, (uint8_t []){0x0F}, 1, 0},
    {0xD0, (uint8_t []){0xA7}, 1, 0},
    {0xD0, (uint8_t []){0xA4, 0xA1}, 2, 0},
    {0xD6, (uint8_t []){0xA1}, 1, 0},
    {0xE0, (uint8_t []){0xF0, 0x05, 0x0E, 0x08, 0x0A, 0x17, 0x39, 0x54,
                        0x4E, 0x37, 0x12, 0x12, 0x31, 0x37}, 14, 0},
    {0xE1, (uint8_t []){0xF0, 0x10, 0x14, 0x0D, 0x0B, 0x05, 0x39, 0x44,
                        0x4D, 0x38, 0x14, 0x14, 0x2E, 0x35}, 14, 0},
    {0xE4, (uint8_t []){0x23, 0x00, 0x00}, 3, 0},
    {0x21, NULL, 0, 0},
    {0x29, NULL, 0, 0},
    {0x2C, NULL, 0, 0},
};

static const ili9341_vendor_config_t s_vendor_config = {
    .init_cmds = s_vendor_init_cmds,
    .init_cmds_size = sizeof(s_vendor_init_cmds) / sizeof(s_vendor_init_cmds[0]),
};

/**
 * @brief Create ILI9341 panel with vendor init and board-specific settings
 */
static esp_err_t bsp_lcd_create_panel(esp_lcd_panel_io_handle_t io,
                                      esp_lcd_panel_handle_t *ret_panel)
{
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = GPIO_NUM_NC,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
        .bits_per_pixel = BSP_LCD_BITS_PER_PIXEL,
        .flags = { .reset_active_high = true },
        .vendor_config = (void *)&s_vendor_config,
    };

    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ili9341(io, &panel_cfg, ret_panel), TAG, "ILI9341 panel create failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(*ret_panel, 36, 0), TAG, "Set gap failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(*ret_panel), TAG, "Panel reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(*ret_panel), TAG, "Panel init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(*ret_panel, false, true), TAG, "Mirror failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(*ret_panel, true), TAG, "Swap XY failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(*ret_panel, true), TAG, "Display on failed");

    return ESP_OK;
}

/* ━━━━━━━━━━━━━━ Board Init ━━━━━━━━━━━━━━ */

esp_err_t bsp_board_init(void)
{
    /* PA amplifier control: default HIGH (enabled) */
    gpio_config_t pa_cfg = {
        .pin_bit_mask = (1ULL << BSP_PA_CTRL),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&pa_cfg), TAG, "PA GPIO config failed");
    ESP_RETURN_ON_ERROR(gpio_set_level(BSP_PA_CTRL, 1), TAG, "PA GPIO set level failed");

    ESP_LOGI(TAG, "Board: ESP-SensairHalo (ESP32-C5) initialized");
    return ESP_OK;
}

/* ━━━━━━━━━━━━━━ I2C ━━━━━━━━━━━━━━ */

esp_err_t bsp_i2c_init(void)
{
    if (s_i2c_handle != NULL) {
        ESP_LOGW(TAG, "I2C already initialized");
        return ESP_OK;
    }

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = BSP_I2C_NUM,
        .sda_io_num = BSP_I2C_SDA,
        .scl_io_num = BSP_I2C_SCL,
        .lp_source_clk = LP_I2C_SCLK_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_i2c_handle), TAG, "I2C master bus create failed");
    ESP_LOGI(TAG, "I2C initialized (port=%d, SDA=%d, SCL=%d)", BSP_I2C_NUM, BSP_I2C_SDA, BSP_I2C_SCL);
    return ESP_OK;
}

esp_err_t bsp_i2c_deinit(void)
{
    if (s_i2c_handle == NULL) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(i2c_del_master_bus(s_i2c_handle), TAG, "I2C delete failed");
    s_i2c_handle = NULL;
    return ESP_OK;
}

esp_err_t bsp_i2c_get_bus_handle(i2c_master_bus_handle_t *handle)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "handle is NULL");
    ESP_RETURN_ON_FALSE(s_i2c_handle != NULL, ESP_ERR_INVALID_STATE, TAG, "I2C not initialized, call bsp_i2c_init() first");
    *handle = s_i2c_handle;
    return ESP_OK;
}

/* ━━━━━━━━━━━━━━ Display (SPI) ━━━━━━━━━━━━━━ */

#if CONFIG_BSP_LCD_SPI
static esp_err_t bsp_display_new_spi(const bsp_display_config_t *config,
                                     esp_lcd_panel_handle_t *ret_panel,
                                     esp_lcd_panel_io_handle_t *ret_io)
{
    int max_transfer_sz = (config && config->max_transfer_sz > 0) ? config->max_transfer_sz : (BSP_LCD_H_RES * 20 * 2);

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = BSP_LCD_SPI_MOSI,
        .miso_io_num = GPIO_NUM_NC,
        .sclk_io_num = BSP_LCD_SPI_CLK,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = max_transfer_sz,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO), TAG, "SPI bus init failed");

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = BSP_LCD_DC,
        .cs_gpio_num = BSP_LCD_SPI_CS,
        .pclk_hz = 40 * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 3,
        .trans_queue_depth = 10,
        .flags = { .sio_mode = true },
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(SPI2_HOST, &io_cfg, ret_io), TAG, "SPI panel IO create failed");
    ESP_RETURN_ON_ERROR(bsp_lcd_create_panel(*ret_io, ret_panel), TAG, "LCD panel create failed");

    ESP_LOGI(TAG, "Display (SPI) initialized: %dx%d", BSP_LCD_H_RES, BSP_LCD_V_RES);
    return ESP_OK;
}
#endif

/* ━━━━━━━━━━━━━━ Display (PARLIO) ━━━━━━━━━━━━━━ */

#if CONFIG_BSP_LCD_PARLIO
static esp_err_t bsp_display_new_parlio(const bsp_display_parlio_config_t *config,
                                        esp_lcd_panel_handle_t *ret_panel,
                                        esp_lcd_panel_io_handle_t *ret_io)
{
    size_t max_transfer = (config && config->max_transfer_bytes > 0) ? config->max_transfer_bytes : (BSP_LCD_H_RES * 20 * 2);
    uint32_t dma_burst = (config && config->dma_burst_size > 0) ? config->dma_burst_size : 32;
    int queue_depth = (config && config->trans_queue_depth > 0) ? config->trans_queue_depth : 10;

    esp_lcd_panel_io_parl_config_t io_cfg = {
        .dc_gpio_num = BSP_LCD_DC,
        .clk_gpio_num = BSP_LCD_SPI_CLK,
        .cs_gpio_num = BSP_LCD_SPI_CS,
        .data_gpio_nums = { BSP_LCD_SPI_MOSI, -1, -1, -1, -1, -1, -1, -1 },
        .data_width = 1,
        .pclk_hz = 40 * 1000 * 1000,
        .clk_src = PARLIO_CLK_SRC_DEFAULT,
        .max_transfer_bytes = max_transfer,
        .dma_burst_size = dma_burst,
        .trans_queue_depth = queue_depth,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .dc_levels = {
            .dc_cmd_level = 0,
            .dc_data_level = 1,
        },
        .flags = {
            .cs_active_high = false,
        },
    };

    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_parl(&io_cfg, ret_io), TAG, "PARLIO panel IO create failed");
    ESP_RETURN_ON_ERROR(bsp_lcd_create_panel(*ret_io, ret_panel), TAG, "LCD panel create failed");

    ESP_LOGI(TAG, "Display (PARLIO) initialized: %dx%d", BSP_LCD_H_RES, BSP_LCD_V_RES);
    return ESP_OK;
}
#endif

/* ━━━━━━━━━━━━━━ Display (unified entry) ━━━━━━━━━━━━━━ */

esp_err_t bsp_display_new(const bsp_display_config_t *spi_config,
                          const bsp_display_parlio_config_t *parlio_config,
                          esp_lcd_panel_handle_t *ret_panel,
                          esp_lcd_panel_io_handle_t *ret_io)
{
    ESP_RETURN_ON_FALSE(ret_panel != NULL && ret_io != NULL, ESP_ERR_INVALID_ARG, TAG, "NULL output pointers");

#if CONFIG_BSP_LCD_SPI
    return bsp_display_new_spi(spi_config, ret_panel, ret_io);
#elif CONFIG_BSP_LCD_PARLIO
    return bsp_display_new_parlio(parlio_config, ret_panel, ret_io);
#else
#error "No LCD bus type selected. Set CONFIG_BSP_LCD_SPI or CONFIG_BSP_LCD_PARLIO in Kconfig."
#endif
}

/* ━━━━━━━━━━━━━━ Display Brightness ━━━━━━━━━━━━━━ */

esp_err_t bsp_display_brightness_set(int brightness_percent)
{
    /* No dedicated backlight pin on ESP-SensairHalo; stub for API compatibility */
    ESP_LOGD(TAG, "Brightness set to %d%% (no backlight control)", brightness_percent);
    return ESP_OK;
}

esp_err_t bsp_display_backlight_on(void)
{
    return bsp_display_brightness_set(100);
}

esp_err_t bsp_display_backlight_off(void)
{
    return bsp_display_brightness_set(0);
}

/* ━━━━━━━━━━━━━━ Touch (CST816S) ━━━━━━━━━━━━━━ */

esp_err_t bsp_touch_new(esp_lcd_touch_handle_t *ret_touch)
{
    ESP_RETURN_ON_FALSE(ret_touch != NULL, ESP_ERR_INVALID_ARG, TAG, "ret_touch is NULL");
    ESP_RETURN_ON_FALSE(s_i2c_handle != NULL, ESP_ERR_INVALID_STATE, TAG, "I2C not initialized");

    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_CST816S_CONFIG();
    io_cfg.dev_addr = 0x15;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(s_i2c_handle, &io_cfg, &io_handle), TAG, "Touch I2C IO create failed");

    esp_lcd_touch_config_t touch_cfg = {
        .x_max = BSP_LCD_H_RES,
        .y_max = BSP_LCD_V_RES,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = GPIO_NUM_NC,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = true,
            .mirror_x = false,
            .mirror_y = true,
        },
    };

    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_cst816s(io_handle, &touch_cfg, ret_touch), TAG, "CST816S create failed");

    ESP_LOGI(TAG, "Touch (CST816S) initialized");
    return ESP_OK;
}

/* ━━━━━━━━━━━━━━ Touch Button (BS8112A3) ━━━━━━━━━━━━━━ */

esp_err_t bsp_touch_button_init(touch_ic_bs8112a3_handle_t *ret_handle)
{
    ESP_RETURN_ON_FALSE(ret_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "ret_handle is NULL");
    ESP_RETURN_ON_FALSE(s_i2c_handle != NULL, ESP_ERR_INVALID_STATE, TAG, "I2C not initialized");

    bs8112a3_chip_config_t chip_cfg = TOUCH_IC_BS8112A3_DEFAULT_CHIP_CONFIG();
    chip_cfg.lsc_mode = BS8112A3_LSC_MODE_NORMAL;
    chip_cfg.irq_oms = BS8112A3_IRQ_OMS_LEVEL_HOLD;
    chip_cfg.k12_mode = BS8112A3_K12_MODE_IRQ;

    touch_ic_bs8112a3_config_t cfg = {
        .i2c_bus_handle = s_i2c_handle,
        .device_address = BS8112A3_I2C_ADDR,
        .scl_speed_hz = 400000,
        .irq_pin = BSP_TOUCH_BUTTON_IRQ,
        .chip_config = &chip_cfg,
    };

    ESP_RETURN_ON_ERROR(touch_ic_bs8112a3_create(&cfg, ret_handle), TAG, "BS8112A3 create failed");

    ESP_LOGI(TAG, "Touch button (BS8112A3) initialized");
    return ESP_OK;
}

/* ━━━━━━━━━━━━━━ Speaker (I2S PDM TX) ━━━━━━━━━━━━━━ */

static int _codec_open(const audio_codec_if_t *h, void *cfg, int cfg_size) { return ESP_OK; }
static bool _codec_is_open(const audio_codec_if_t *h) { return true; }
static int _codec_set_mic_gain(const audio_codec_if_t *h, float db) { return ESP_OK; }
static int _codec_set_vol(const audio_codec_if_t *h, float db) { return ESP_OK; }

static audio_codec_if_t s_codec_if = {
    .open = _codec_open,
    .is_open = _codec_is_open,
    .set_mic_gain = _codec_set_mic_gain,
    .set_vol = _codec_set_vol,
};

#define BSP_I2S_GPIO_CFG(_dout) { .clk = GPIO_NUM_NC, .dout = _dout, .invert_flags = { .clk_inv = false } }

esp_err_t bsp_speaker_init(esp_codec_dev_handle_t *ret_handle)
{
    ESP_RETURN_ON_FALSE(ret_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "ret_handle is NULL");
    ESP_RETURN_ON_FALSE(s_speaker_tx == NULL, ESP_ERR_INVALID_STATE, TAG, "Speaker already initialized");

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_speaker_tx, NULL), TAG, "I2S channel create failed");

    i2s_pdm_tx_config_t pdm_cfg = {
        .clk_cfg = I2S_PDM_TX_CLK_DEFAULT_CONFIG(CONFIG_BSP_AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_PDM_TX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = BSP_I2S_GPIO_CFG(BSP_SPEAKER_P),
    };
    pdm_cfg.clk_cfg.up_sample_fs = 480;
    pdm_cfg.slot_cfg.sd_scale = I2S_PDM_SIG_SCALING_MUL_4;
    pdm_cfg.slot_cfg.hp_scale = I2S_PDM_SIG_SCALING_MUL_4;
    pdm_cfg.slot_cfg.lp_scale = I2S_PDM_SIG_SCALING_MUL_4;
    pdm_cfg.slot_cfg.sinc_scale = I2S_PDM_SIG_SCALING_MUL_4;

    ESP_RETURN_ON_ERROR(i2s_channel_init_pdm_tx_mode(s_speaker_tx, &pdm_cfg), TAG, "PDM TX init failed");

    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = I2S_NUM_0,
        .rx_handle = NULL,
        .tx_handle = s_speaker_tx,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    ESP_RETURN_ON_FALSE(data_if != NULL, ESP_FAIL, TAG, "I2S data interface create failed");

    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_speaker_tx), TAG, "I2S channel enable failed");

    gpio_set_drive_capability(BSP_SPEAKER_P, GPIO_DRIVE_CAP_0);

    /* Connect speaker negative pin with inverted signal */
    PIN_FUNC_SELECT(IO_MUX_GPIO10_REG, PIN_FUNC_GPIO);
    gpio_set_direction(BSP_SPEAKER_N, GPIO_MODE_OUTPUT);
    esp_rom_gpio_connect_out_signal(BSP_SPEAKER_N, I2SO_SD_OUT_IDX, 1, 0);
    gpio_set_drive_capability(BSP_SPEAKER_N, GPIO_DRIVE_CAP_0);

    esp_codec_dev_cfg_t codec_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .data_if = data_if,
        .codec_if = &s_codec_if,
    };
    s_speaker_codec = esp_codec_dev_new(&codec_cfg);
    ESP_RETURN_ON_FALSE(s_speaker_codec != NULL, ESP_FAIL, TAG, "Speaker codec create failed");

    *ret_handle = s_speaker_codec;
    ESP_LOGI(TAG, "Speaker (PDM) initialized (P=%d, N=%d)", BSP_SPEAKER_P, BSP_SPEAKER_N);
    return ESP_OK;
}

esp_err_t bsp_speaker_deinit(void)
{
    if (s_speaker_codec) {
        esp_codec_dev_close(s_speaker_codec);
        s_speaker_codec = NULL;
    }
    if (s_speaker_tx) {
        esp_err_t ret = i2s_channel_disable(s_speaker_tx);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "Speaker channel disable failed: %s", esp_err_to_name(ret));
        }
        i2s_del_channel(s_speaker_tx);
        s_speaker_tx = NULL;
    }
    return ESP_OK;
}

esp_err_t bsp_pa_enable(bool enable)
{
    return gpio_set_level(BSP_PA_CTRL, enable ? 1 : 0);
}

/* ━━━━━━━━━━━━━━ Microphone (ADC) ━━━━━━━━━━━━━━ */

esp_err_t bsp_microphone_init(esp_codec_dev_handle_t *ret_handle)
{
    ESP_RETURN_ON_FALSE(ret_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "ret_handle is NULL");
    ESP_RETURN_ON_FALSE(s_mic_codec == NULL, ESP_ERR_INVALID_STATE, TAG, "Mic already initialized");

    audio_codec_adc_cfg_t adc_cfg = DEFAULT_AUDIO_CODEC_ADC_MONO_CFG(CONFIG_BSP_ADC_MIC_CHANNEL, CONFIG_BSP_AUDIO_SAMPLE_RATE);
    const audio_codec_data_if_t *adc_if = audio_codec_new_adc_data(&adc_cfg);
    ESP_RETURN_ON_FALSE(adc_if != NULL, ESP_FAIL, TAG, "ADC data interface create failed");

    esp_codec_dev_cfg_t codec_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
        .data_if = adc_if,
        .codec_if = &s_codec_if,
    };
    s_mic_codec = esp_codec_dev_new(&codec_cfg);
    ESP_RETURN_ON_FALSE(s_mic_codec != NULL, ESP_FAIL, TAG, "Mic codec create failed");

    *ret_handle = s_mic_codec;
    ESP_LOGI(TAG, "Microphone (ADC ch%d) initialized", CONFIG_BSP_ADC_MIC_CHANNEL);
    return ESP_OK;
}

esp_err_t bsp_microphone_deinit(void)
{
    if (s_mic_codec) {
        esp_codec_dev_close(s_mic_codec);
        s_mic_codec = NULL;
    }
    return ESP_OK;
}

/* ━━━━━━━━━━━━━━ IMU — BMI270 ━━━━━━━━━━━━━━ */

#define BMI270_I2C_CLK_SPEED  100000

static esp_err_t bsp_bmi270_configure_int1(void)
{
    struct bmi2_int_pin_config pin_config = { 0 };
    int8_t rslt = bmi2_get_int_pin_config(&pin_config, s_bmi_handle);
    ESP_RETURN_ON_FALSE(rslt == BMI2_OK, ESP_FAIL, TAG, "BMI270 get INT pin config failed: %d", rslt);

    pin_config.pin_type = BMI2_INT1;
    pin_config.pin_cfg[0].input_en = BMI2_INT_INPUT_DISABLE;
    pin_config.pin_cfg[0].lvl = BMI2_INT_ACTIVE_LOW;
    pin_config.pin_cfg[0].od = BMI2_INT_PUSH_PULL;
    pin_config.pin_cfg[0].output_en = BMI2_INT_OUTPUT_ENABLE;
    pin_config.int_latch = BMI2_INT_NON_LATCH;

    rslt = bmi2_set_int_pin_config(&pin_config, s_bmi_handle);
    ESP_RETURN_ON_FALSE(rslt == BMI2_OK, ESP_FAIL, TAG, "BMI270 set INT1 config failed: %d", rslt);
    return ESP_OK;
}

static esp_err_t bsp_bmi270_map_feature_interrupts(const uint8_t *feature_list, size_t feature_count)
{
    ESP_RETURN_ON_FALSE(feature_list != NULL && feature_count > 0, ESP_ERR_INVALID_ARG, TAG,
                        "invalid feature interrupt map request");

    struct bmi2_sens_int_config sens_int[BMI270_MAX_INT_MAP] = { 0 };
    ESP_RETURN_ON_FALSE(feature_count <= BMI270_MAX_INT_MAP, ESP_ERR_INVALID_ARG, TAG,
                        "too many BMI270 feature interrupts: %u", (unsigned)feature_count);

    for (size_t i = 0; i < feature_count; i++) {
        sens_int[i].type = feature_list[i];
        sens_int[i].hw_int_pin = BMI2_INT1;
    }

    int8_t rslt = bmi270_map_feat_int(sens_int, (uint8_t)feature_count, s_bmi_handle);
    ESP_RETURN_ON_FALSE(rslt == BMI2_OK, ESP_FAIL, TAG, "BMI270 map feature interrupt failed: %d", rslt);
    return ESP_OK;
}

esp_err_t bsp_imu_init(bmi270_handle_t *ret_handle)
{
    ESP_RETURN_ON_FALSE(ret_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "ret_handle is NULL");
    ESP_RETURN_ON_FALSE(s_bmi_handle == NULL, ESP_ERR_INVALID_STATE, TAG, "IMU already initialized");
    ESP_RETURN_ON_FALSE(s_i2c_handle != NULL, ESP_ERR_INVALID_STATE, TAG, "I2C not initialized");

    if (s_imu_i2c_bus == NULL) {
        const i2c_config_t i2c_conf = {
            .mode = I2C_MODE_MASTER,
            .sda_io_num = BSP_I2C_SDA,
            .scl_io_num = BSP_I2C_SCL,
            .sda_pullup_en = GPIO_PULLUP_ENABLE,
            .scl_pullup_en = GPIO_PULLUP_ENABLE,
            .master = {.clk_speed = BMI270_I2C_CLK_SPEED},
            .clk_flags = 0,
        };
        s_imu_i2c_bus = i2c_bus_create(BSP_I2C_NUM, &i2c_conf);
        ESP_RETURN_ON_FALSE(s_imu_i2c_bus != NULL, ESP_FAIL, TAG, "IMU I2C bus create failed");
    }

    bmi270_i2c_config_t bmi_i2c_conf = {
        .i2c_handle = s_imu_i2c_bus,
        .i2c_addr = BMI2_I2C_PRIM_ADDR,
    };
    ESP_RETURN_ON_ERROR(bmi270_sensor_create(&bmi_i2c_conf, &s_bmi_handle), TAG, "BMI270 create failed");

    int8_t rslt = bmi2_set_adv_power_save(BMI2_DISABLE, s_bmi_handle);
    if (rslt != BMI2_OK) {
        ESP_LOGW(TAG, "Failed to disable adv power save: %d", rslt);
    }

    uint8_t sens_list[2] = { BMI2_ACCEL, BMI2_GYRO };
    struct bmi2_sens_config config[2];
    config[0].type = BMI2_ACCEL;
    config[1].type = BMI2_GYRO;
    rslt = bmi2_get_sensor_config(config, 2, s_bmi_handle);
    ESP_RETURN_ON_FALSE(rslt == BMI2_OK, ESP_FAIL, TAG, "BMI270 get accel/gyro config failed: %d", rslt);

    config[0].cfg.acc.odr = BMI2_ACC_ODR_100HZ;
    config[0].cfg.acc.range = BMI2_ACC_RANGE_4G;
    config[0].cfg.acc.bwp = BMI2_ACC_NORMAL_AVG4;
    config[0].cfg.acc.filter_perf = BMI2_PERF_OPT_MODE;

    config[1].cfg.gyr.odr = BMI2_GYR_ODR_100HZ;
    config[1].cfg.gyr.range = BMI2_GYR_RANGE_2000;
    config[1].cfg.gyr.bwp = BMI2_GYR_NORMAL_MODE;
    config[1].cfg.gyr.noise_perf = BMI2_POWER_OPT_MODE;
    config[1].cfg.gyr.filter_perf = BMI2_PERF_OPT_MODE;

    rslt = bmi2_set_sensor_config(config, 2, s_bmi_handle);
    ESP_RETURN_ON_FALSE(rslt == BMI2_OK, ESP_FAIL, TAG, "BMI270 set accel/gyro config failed: %d", rslt);

    rslt = bmi2_sensor_enable(sens_list, 2, s_bmi_handle);
    ESP_RETURN_ON_FALSE(rslt == BMI2_OK, ESP_FAIL, TAG, "BMI270 sensor enable failed: %d", rslt);
    ESP_RETURN_ON_ERROR(bsp_bmi270_configure_int1(), TAG, "BMI270 INT1 config failed");

    *ret_handle = s_bmi_handle;
    ESP_LOGI(TAG, "IMU (BMI270) initialized: Accel 4G@100Hz, Gyro 2000dps@100Hz, INT1->GPIO%d",
             BSP_IMU_INT);
    return ESP_OK;
}

esp_err_t bsp_imu_deinit(void)
{
    if (s_bmi_handle != NULL) {
        bmi270_sensor_del(s_bmi_handle);
        s_bmi_handle = NULL;
    }
    if (s_imu_i2c_bus != NULL) {
        i2c_bus_delete(&s_imu_i2c_bus);
        s_imu_i2c_bus = NULL;
    }
    s_bmm_initialized = false;
    return ESP_OK;
}

bmi270_handle_t bsp_imu_get_handle(void)
{
    return s_bmi_handle;
}

/* ━━━━━━━━━━━━━━ IMU — BMI270 Extended Features ━━━━━━━━━━━━━━ */

static const char *s_activity_str[4] = { "still", "walking", "running", "unknown" };
static const char *s_gesture_str[6]  = {
    "unknown_gesture", "push_arm_down", "pivot_up",
    "wrist_shake_jiggle", "flick_in", "flick_out"
};
static const uint16_t kAnyMotionThreshold = UINT16_C(0x0400);
static const uint16_t kAnyMotionDuration = UINT16_C(0x05);  /* 5 * 20 ms = 100 ms */

esp_err_t bsp_imu_enable_step_counter(void)
{
    ESP_RETURN_ON_FALSE(s_bmi_handle != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "IMU not initialized, call bsp_imu_init first");

    uint8_t sens_list[4] = { BMI2_ACCEL, BMI2_STEP_DETECTOR, BMI2_STEP_COUNTER, BMI2_STEP_ACTIVITY };
    static const uint8_t feature_list[3] = { BMI2_STEP_DETECTOR, BMI2_STEP_COUNTER, BMI2_STEP_ACTIVITY };

    int8_t rslt = bmi270_sensor_enable(sens_list, 4, s_bmi_handle);
    ESP_RETURN_ON_FALSE(rslt == BMI2_OK, ESP_FAIL, TAG,
                        "Failed to enable step counter features: %d", rslt);
    ESP_RETURN_ON_ERROR(bsp_bmi270_map_feature_interrupts(feature_list, 3), TAG,
                        "Step feature interrupt map failed");

    ESP_LOGI(TAG, "Step counter/detector/activity enabled");
    return ESP_OK;
}

esp_err_t bsp_imu_enable_wrist_gesture(uint8_t wearable_arm)
{
    ESP_RETURN_ON_FALSE(s_bmi_handle != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "IMU not initialized, call bsp_imu_init first");

    uint8_t sens_list[2] = { BMI2_ACCEL, BMI2_WRIST_GESTURE };
    struct bmi2_sens_config cfg = { 0 };
    cfg.type = BMI2_WRIST_GESTURE;
    int8_t rslt = bmi270_sensor_enable(sens_list, 2, s_bmi_handle);
    ESP_RETURN_ON_FALSE(rslt == BMI2_OK, ESP_FAIL, TAG, "Failed to enable wrist gesture sensors: %d", rslt);

    rslt = bmi270_get_sensor_config(&cfg, 1, s_bmi_handle);
    ESP_RETURN_ON_FALSE(rslt == BMI2_OK, ESP_FAIL, TAG, "Failed to get wrist gesture config: %d", rslt);

    cfg.cfg.wrist_gest.wearable_arm = (wearable_arm != 0) ? 1 : 0;
    rslt = bmi270_set_sensor_config(&cfg, 1, s_bmi_handle);
    ESP_RETURN_ON_FALSE(rslt == BMI2_OK, ESP_FAIL, TAG, "Failed to set wrist gesture config: %d", rslt);

    uint8_t feat = BMI2_WRIST_GESTURE;
    ESP_RETURN_ON_ERROR(bsp_bmi270_map_feature_interrupts(&feat, 1), TAG,
                        "Wrist gesture interrupt map failed");

    ESP_LOGI(TAG, "Wrist gesture enabled (arm=%s)", wearable_arm ? "right" : "left");
    return ESP_OK;
}

esp_err_t bsp_imu_enable_motion_detect(void)
{
    ESP_RETURN_ON_FALSE(s_bmi_handle != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "IMU not initialized, call bsp_imu_init first");

    uint8_t sens_list[3] = { BMI2_ACCEL, BMI2_ANY_MOTION, BMI2_NO_MOTION };
    struct bmi2_sens_config cfg[2] = { 0 };
    cfg[0].type = BMI2_ANY_MOTION;
    cfg[1].type = BMI2_NO_MOTION;
    int8_t rslt = bmi270_sensor_enable(sens_list, 3, s_bmi_handle);
    ESP_RETURN_ON_FALSE(rslt == BMI2_OK, ESP_FAIL, TAG, "Failed to enable motion detection sensors: %d", rslt);

    rslt = bmi270_get_sensor_config(cfg, 2, s_bmi_handle);
    ESP_RETURN_ON_FALSE(rslt == BMI2_OK, ESP_FAIL, TAG, "Failed to get motion detection config: %d", rslt);

    cfg[0].cfg.any_motion.duration = kAnyMotionDuration;
    cfg[0].cfg.any_motion.threshold = kAnyMotionThreshold;
    cfg[0].cfg.any_motion.select_x = 1;
    cfg[0].cfg.any_motion.select_y = 1;
    cfg[0].cfg.any_motion.select_z = 1;
    cfg[1].cfg.no_motion.duration = kAnyMotionDuration;
    cfg[1].cfg.no_motion.threshold = kAnyMotionThreshold;
    cfg[1].cfg.no_motion.select_x = 1;
    cfg[1].cfg.no_motion.select_y = 1;
    cfg[1].cfg.no_motion.select_z = 1;
    rslt = bmi270_set_sensor_config(cfg, 2, s_bmi_handle);
    ESP_RETURN_ON_FALSE(rslt == BMI2_OK, ESP_FAIL, TAG, "Failed to set motion detection config: %d", rslt);

    static const uint8_t feature_list[2] = { BMI2_ANY_MOTION, BMI2_NO_MOTION };
    ESP_RETURN_ON_ERROR(bsp_bmi270_map_feature_interrupts(feature_list, 2), TAG,
                        "Motion feature interrupt map failed");

    ESP_LOGI(TAG, "Any-motion / no-motion detection enabled (threshold=0x%03X, duration=%u)",
             kAnyMotionThreshold, kAnyMotionDuration);
    return ESP_OK;
}

esp_err_t bsp_imu_enable_sig_motion(void)
{
    ESP_RETURN_ON_FALSE(s_bmi_handle != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "IMU not initialized, call bsp_imu_init first");

    uint8_t sens_list[2] = { BMI2_ACCEL, BMI2_SIG_MOTION };
    struct bmi2_sens_config cfg = { 0 };
    cfg.type = BMI2_SIG_MOTION;

    int8_t rslt = bmi270_sensor_enable(sens_list, 2, s_bmi_handle);
    ESP_RETURN_ON_FALSE(rslt == BMI2_OK, ESP_FAIL, TAG, "Failed to enable significant motion: %d", rslt);

    rslt = bmi270_get_sensor_config(&cfg, 1, s_bmi_handle);
    ESP_RETURN_ON_FALSE(rslt == BMI2_OK, ESP_FAIL, TAG, "Failed to get significant motion config: %d", rslt);

    rslt = bmi270_set_sensor_config(&cfg, 1, s_bmi_handle);
    ESP_RETURN_ON_FALSE(rslt == BMI2_OK, ESP_FAIL, TAG, "Failed to set significant motion config: %d", rslt);

    uint8_t feat = BMI2_SIG_MOTION;
    ESP_RETURN_ON_ERROR(bsp_bmi270_map_feature_interrupts(&feat, 1), TAG,
                        "Significant motion interrupt map failed");

    ESP_LOGI(TAG, "Significant motion detection enabled");
    return ESP_OK;
}

esp_err_t bsp_imu_enable_wrist_wear_wakeup(void)
{
    ESP_RETURN_ON_FALSE(s_bmi_handle != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "IMU not initialized, call bsp_imu_init first");

    uint8_t sens_list[2] = { BMI2_ACCEL, BMI2_WRIST_WEAR_WAKE_UP };
    struct bmi2_sens_config cfg = { 0 };
    cfg.type = BMI2_WRIST_WEAR_WAKE_UP;

    int8_t rslt = bmi270_sensor_enable(sens_list, 2, s_bmi_handle);
    ESP_RETURN_ON_FALSE(rslt == BMI2_OK, ESP_FAIL, TAG, "Failed to enable wrist wear wake-up: %d", rslt);

    rslt = bmi270_get_sensor_config(&cfg, 1, s_bmi_handle);
    ESP_RETURN_ON_FALSE(rslt == BMI2_OK, ESP_FAIL, TAG, "Failed to get wrist wear wake-up config: %d", rslt);

    rslt = bmi270_set_sensor_config(&cfg, 1, s_bmi_handle);
    ESP_RETURN_ON_FALSE(rslt == BMI2_OK, ESP_FAIL, TAG, "Failed to set wrist wear wake-up config: %d", rslt);

    uint8_t feat = BMI2_WRIST_WEAR_WAKE_UP;
    ESP_RETURN_ON_ERROR(bsp_bmi270_map_feature_interrupts(&feat, 1), TAG,
                        "Wrist wear wake-up interrupt map failed");

    ESP_LOGI(TAG, "Wrist wear wake-up enabled");
    return ESP_OK;
}

esp_err_t bsp_imu_get_step_count(uint32_t *step_count)
{
    ESP_RETURN_ON_FALSE(step_count != NULL, ESP_ERR_INVALID_ARG, TAG, "step_count is NULL");
    ESP_RETURN_ON_FALSE(s_bmi_handle != NULL, ESP_ERR_INVALID_STATE, TAG, "IMU not initialized");

    struct bmi2_feat_sensor_data feat_data = { .type = BMI2_STEP_COUNTER };
    int8_t rslt = bmi270_get_feature_data(&feat_data, 1, s_bmi_handle);
    ESP_RETURN_ON_FALSE(rslt == BMI2_OK, ESP_FAIL, TAG, "Failed to read step count: %d", rslt);

    *step_count = feat_data.sens_data.step_counter_output;
    return ESP_OK;
}

esp_err_t bsp_imu_get_step_activity(uint8_t *activity, const char **activity_str)
{
    ESP_RETURN_ON_FALSE(activity != NULL && activity_str != NULL,
                        ESP_ERR_INVALID_ARG, TAG, "NULL argument");
    ESP_RETURN_ON_FALSE(s_bmi_handle != NULL, ESP_ERR_INVALID_STATE, TAG, "IMU not initialized");

    struct bmi2_feat_sensor_data feat_data = { .type = BMI2_STEP_ACTIVITY };
    int8_t rslt = bmi270_get_feature_data(&feat_data, 1, s_bmi_handle);
    ESP_RETURN_ON_FALSE(rslt == BMI2_OK, ESP_FAIL, TAG, "Failed to read step activity: %d", rslt);

    uint8_t act = feat_data.sens_data.activity_output;
    *activity = act;
    *activity_str = (act < 4) ? s_activity_str[act] : "unknown";
    return ESP_OK;
}

esp_err_t bsp_imu_get_wrist_gesture(uint8_t *gesture, const char **gesture_str)
{
    ESP_RETURN_ON_FALSE(gesture != NULL && gesture_str != NULL,
                        ESP_ERR_INVALID_ARG, TAG, "NULL argument");
    ESP_RETURN_ON_FALSE(s_bmi_handle != NULL, ESP_ERR_INVALID_STATE, TAG, "IMU not initialized");

    struct bmi2_feat_sensor_data feat_data = { .type = BMI2_WRIST_GESTURE };
    int8_t rslt = bmi270_get_feature_data(&feat_data, 1, s_bmi_handle);
    ESP_RETURN_ON_FALSE(rslt == BMI2_OK, ESP_FAIL, TAG, "Failed to read wrist gesture: %d", rslt);

    uint8_t gest = feat_data.sens_data.wrist_gesture_output;
    *gesture = gest;
    *gesture_str = (gest < 6) ? s_gesture_str[gest] : "unknown_gesture";
    return ESP_OK;
}

esp_err_t bsp_imu_get_event_status(uint16_t *int_status)
{
    ESP_RETURN_ON_FALSE(int_status != NULL, ESP_ERR_INVALID_ARG, TAG, "int_status is NULL");
    ESP_RETURN_ON_FALSE(s_bmi_handle != NULL, ESP_ERR_INVALID_STATE, TAG, "IMU not initialized");

    *int_status = 0;
    int8_t rslt = bmi2_get_int_status(int_status, s_bmi_handle);
    ESP_RETURN_ON_FALSE(rslt == BMI2_OK, ESP_FAIL, TAG, "Failed to read interrupt status: %d", rslt);
    return ESP_OK;
}

esp_err_t bsp_imu_get_motion_status(bool *any_motion_triggered, bool *no_motion_triggered)
{
    ESP_RETURN_ON_FALSE(any_motion_triggered != NULL && no_motion_triggered != NULL,
                        ESP_ERR_INVALID_ARG, TAG, "NULL argument");
    uint16_t int_status = 0;
    ESP_RETURN_ON_ERROR(bsp_imu_get_event_status(&int_status), TAG,
                        "Failed to get motion interrupt status");

    *any_motion_triggered = (int_status & BMI270_ANY_MOT_STATUS_MASK) != 0;
    *no_motion_triggered = (int_status & BMI270_NO_MOT_STATUS_MASK) != 0;
    return ESP_OK;
}

/* ━━━━━━━━━━━━━━ Magnetometer — BMM150 (via BMI270 AUX) ━━━━━━━━━━━━━━ */

#define BMM150_I2C_ADDR      0x10
#define BMM150_CHIP_ID_REG   0x40

static esp_err_t bsp_bmi270_aux_setup(void)
{
    ESP_RETURN_ON_FALSE(s_bmi_handle != NULL, ESP_ERR_INVALID_STATE, TAG, "BMI270 not initialized");

    struct bmi2_sens_config aux_cfg = { 0 };
    aux_cfg.type = BMI2_AUX;
    aux_cfg.cfg.aux.aux_en = 1;
    aux_cfg.cfg.aux.manual_en = 1;
    aux_cfg.cfg.aux.fcu_write_en = 0;
    aux_cfg.cfg.aux.man_rd_burst = 1;
    aux_cfg.cfg.aux.aux_rd_burst = 1;
    aux_cfg.cfg.aux.odr = 2;
    aux_cfg.cfg.aux.offset = 0;
    aux_cfg.cfg.aux.i2c_device_addr = BMM150_I2C_ADDR;
    aux_cfg.cfg.aux.read_addr = BMM150_CHIP_ID_REG;

    int8_t rslt = bmi2_set_sensor_config(&aux_cfg, 1, s_bmi_handle);
    ESP_RETURN_ON_FALSE(rslt == BMI2_OK, ESP_FAIL, TAG, "BMI270 AUX config failed: %d", rslt);

    uint8_t aux_sensor = BMI2_AUX;
    rslt = bmi2_sensor_enable(&aux_sensor, 1, s_bmi_handle);
    ESP_RETURN_ON_FALSE(rslt == BMI2_OK, ESP_FAIL, TAG, "BMI270 AUX enable failed: %d", rslt);

    vTaskDelay(pdMS_TO_TICKS(10));
    return ESP_OK;
}

esp_err_t bsp_mag_init(bmm150_aux_handle_t *ret_handle)
{
    ESP_RETURN_ON_FALSE(ret_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "ret_handle is NULL");
    ESP_RETURN_ON_FALSE(!s_bmm_initialized, ESP_ERR_INVALID_STATE, TAG, "MAG already initialized");
    ESP_RETURN_ON_FALSE(s_bmi_handle != NULL, ESP_ERR_INVALID_STATE, TAG, "BMI270 not initialized, call bsp_imu_init first");

    ESP_RETURN_ON_ERROR(bsp_bmi270_aux_setup(), TAG, "BMI270 AUX setup failed");

    bmm150_aux_config_t bmm_config = {
        .bmi2_dev = s_bmi_handle,
        .i2c_addr = BMM150_I2C_ADDR,
        .chip_id_reg = BMM150_CHIP_ID_REG,
    };

    int8_t rslt = bmm150_aux_adapter_init(&bmm_config, &s_bmm_handle);
    ESP_RETURN_ON_FALSE(rslt == BMM150_OK, ESP_FAIL, TAG, "BMM150 AUX adapter init failed: %d", rslt);

    struct bmm150_settings settings = {0};
    settings.pwr_mode = BMM150_POWERMODE_NORMAL;
    settings.data_rate = BMM150_DATA_RATE_10HZ;
    settings.xy_rep = 9;
    settings.z_rep = 15;
    rslt = bmm150_aux_adapter_configure(&s_bmm_handle, &settings);
    if (rslt != BMM150_OK) {
        ESP_LOGW(TAG, "BMM150 configure warning: %d", rslt);
    }

    s_bmm_initialized = true;
    *ret_handle = s_bmm_handle;
    ESP_LOGI(TAG, "Magnetometer (BMM150 via BMI270 AUX) initialized");
    return ESP_OK;
}

esp_err_t bsp_mag_deinit(void)
{
    if (s_bmm_initialized) {
        bmm150_aux_adapter_deinit(&s_bmm_handle);
        s_bmm_initialized = false;
    }
    return ESP_OK;
}

/* ━━━━━━━━━━━━━━ LED Strip (WS2812) ━━━━━━━━━━━━━━ */

esp_err_t bsp_led_strip_init(led_strip_handle_t *ret_handle)
{
    ESP_RETURN_ON_FALSE(ret_handle != NULL, ESP_ERR_INVALID_ARG, TAG, "ret_handle is NULL");
    ESP_RETURN_ON_FALSE(s_led_strip == NULL, ESP_ERR_INVALID_STATE, TAG, "LED strip already initialized");

    led_strip_config_t strip_cfg = {
        .strip_gpio_num = BSP_LED_STRIP_GPIO,
        .max_leds = BSP_LED_STRIP_MAX_LEDS,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };

    led_strip_rmt_config_t rmt_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };

    ESP_RETURN_ON_ERROR(led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_led_strip), TAG, "LED strip create failed");
    ESP_RETURN_ON_ERROR(led_strip_clear(s_led_strip), TAG, "LED strip clear failed");

    *ret_handle = s_led_strip;
    ESP_LOGI(TAG, "LED strip (WS2812 x%d) initialized on GPIO%d", BSP_LED_STRIP_MAX_LEDS, BSP_LED_STRIP_GPIO);
    return ESP_OK;
}

esp_err_t bsp_led_strip_deinit(void)
{
    if (s_led_strip) {
        led_strip_clear(s_led_strip);
        led_strip_del(s_led_strip);
        s_led_strip = NULL;
    }
    return ESP_OK;
}

/* ━━━━━━━━━━━━━━ Camera (BF3901, SPI) ━━━━━━━━━━━━━━ */

esp_err_t bsp_camera_apply_default_controls(int fd)
{
#if !BSP_CAMERA_SUPPORTED
    (void)fd;
    ESP_LOGE(TAG, "Camera support not available (esp_video component not found)");
    return ESP_ERR_NOT_SUPPORTED;
#else
    struct v4l2_ext_control control[1] = {0};
    struct v4l2_ext_controls controls = {
        .ctrl_class = V4L2_CTRL_CLASS_USER,
        .count = 1,
        .controls = control,
    };
    esp_err_t ret = ESP_OK;

    ESP_RETURN_ON_FALSE(fd >= 0, ESP_ERR_INVALID_ARG, TAG, "invalid camera fd");

    control[0].id = V4L2_CID_VFLIP;
    control[0].value = 0;
    if (ioctl(fd, VIDIOC_S_EXT_CTRLS, &controls) != 0) {
        ESP_LOGW(TAG, "Failed to set camera VFLIP=%d, skip", control[0].value);
        ret = ESP_FAIL;
    }

    control[0].id = V4L2_CID_HFLIP;
    control[0].value = 0;
    if (ioctl(fd, VIDIOC_S_EXT_CTRLS, &controls) != 0) {
        ESP_LOGW(TAG, "Failed to set camera HFLIP=%d, skip", control[0].value);
        ret = ESP_FAIL;
    }

    return ret;
#endif
}

esp_err_t bsp_camera_init(void)
{
#if !BSP_CAMERA_SUPPORTED
    ESP_LOGE(TAG, "Camera support not available (esp_video component not found)");
    return ESP_ERR_NOT_SUPPORTED;
#else
    ESP_RETURN_ON_FALSE(!s_camera_initialized, ESP_ERR_INVALID_STATE, TAG, "Camera already initialized");
    ESP_RETURN_ON_FALSE(s_i2c_handle != NULL, ESP_ERR_INVALID_STATE, TAG, "I2C not initialized for camera SCCB");

    esp_video_init_spi_config_t spi_config = {
        .sccb_config.init_sccb = false,
        .sccb_config.i2c_handle = s_i2c_handle,
        .sccb_config.freq = 100000,
        .intf = ESP_CAM_CTLR_SPI_CAM_INTF_SPI,
        .io_mode = ESP_CAM_CTLR_SPI_CAM_IO_MODE_1BIT,
        .spi_port = 1,
        .spi_cs_pin = BSP_CAM_SPI_CS,
        .spi_sclk_pin = BSP_CAM_SPI_SCLK,
        .spi_data0_io_pin = BSP_CAM_SPI_DATA0,
        .spi_data1_io_pin = GPIO_NUM_NC,
        .reset_pin = GPIO_NUM_NC,
        .pwdn_pin = GPIO_NUM_NC,
        .xclk_source = ESP_CAM_SENSOR_XCLK_LEDC,
        .xclk_freq = BSP_CAM_XCLK_FREQ_HZ,
        .xclk_pin = BSP_CAM_XCLK,
#if CONFIG_CAMERA_XCLK_USE_LEDC
        .xclk_ledc_cfg = {
            .timer = CONFIG_BSP_CAM_XCLK_LEDC_TIMER,
            .clk_cfg = LEDC_AUTO_CLK,
            .channel = CONFIG_BSP_CAM_XCLK_LEDC_CHANNEL,
        },
#endif
    };
    const esp_video_init_config_t cam_config = {
        .spi = &spi_config,
    };

    ESP_RETURN_ON_ERROR(esp_video_init(&cam_config), TAG, "SPI camera init failed");
    s_camera_initialized = true;

    ESP_LOGI(TAG, "Camera (BF3901, SPI) initialized, dev_path: %s", ESP_VIDEO_SPI_DEVICE_NAME);
    return ESP_OK;
#endif
}

esp_err_t bsp_camera_deinit(void)
{
#if BSP_CAMERA_SUPPORTED
    if (s_camera_initialized) {
        esp_video_deinit();
        s_camera_initialized = false;
    }
#endif
    return ESP_OK;
}

const char *bsp_camera_get_dev_path(void)
{
#if BSP_CAMERA_SUPPORTED
    return s_camera_initialized ? ESP_VIDEO_SPI_DEVICE_NAME : NULL;
#else
    return NULL;
#endif
}
