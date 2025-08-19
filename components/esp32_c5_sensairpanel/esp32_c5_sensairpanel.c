/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <math.h>
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "driver/rmt_tx.h"
#include "led_strip.h"
#include "led_strip_interface.h"
#include "esp_sleep.h"
#include "touch_ic_bs8112a3.h"
#include "light_sensor_rpr0521.h"
#include "soc/gpio_sig_map.h"

#include "adc_mic.h"
#include <stdlib.h>
#include "hal/gpio_ll.h"
#include "audio_player.h"
#include "file_iterator.h"

#include "ir_remote_control.h"

#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "bsp_err_check.h"
#include "esp_lcd_gc9a01.h"
#include "esp_lvgl_port.h"
#include "esp_codec_dev_defaults.h"

static const char *TAG = "ESP32-C5-Sensairpanel";

/**
 * @brief ESP32-C5-Sensairpanel I2S pinout
 *
 * Can be used for i2s_pdm_tx_gpio_config_t and/or i2s_pdm_tx_config_t initialization
 */
#define BSP_I2S_GPIO_CFG(_dout)   \
    {                          \
        .clk = GPIO_NUM_NC,    \
        .dout = _dout,         \
        .invert_flags = {      \
            .clk_inv = false,  \
        },                     \
    }

/**
 * @brief Mono Duplex I2S configuration structure
 *
 * This configuration is used by default in bsp_audio_init()
 */
#define BSP_I2S_DUPLEX_MONO_CFG(_sample_rate, _dout)                                            \
    {                                                                                        \
        .clk_cfg = I2S_PDM_TX_CLK_DEFAULT_CONFIG(_sample_rate),                             \
        .slot_cfg = I2S_PDM_TX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO), \
        .gpio_cfg = BSP_I2S_GPIO_CFG(_dout),                                                \
    }

// Button
static button_handle_t bsp_button_handles[BSP_INPUT_MAX] = {NULL};
static bsp_button_callback_t global_button_callback = NULL;
static bool touch_hardware_available = false;  // Track touch hardware availability

// I2C
static i2c_master_bus_handle_t s_i2c_bus_handle = NULL;

// Light Sensor
static bool light_sensor_available = false;  // Track light sensor availability

// LED
static led_strip_handle_t led_strip = NULL;
static bsp_led_config_t led_configs[BSP_LED_STRIP_COUNT];
static esp_timer_handle_t led_effect_timer = NULL;
static bsp_led_effect_t current_effect = BSP_LED_EFFECT_STATIC;
static bool effect_running = false;
static uint8_t effect_step = 0;

// LED Matrix (16x16 on BSP_RGB_EXT_CTRL)
static led_strip_handle_t s_led_mx = NULL;
static uint32_t s_led_mx_fb[BSP_LED_MATRIX_ROWS * BSP_LED_MATRIX_COLS];
static uint8_t s_led_mx_brightness = 160; // 0-255
static esp_timer_handle_t s_led_mx_timer = NULL;
static bsp_led_matrix_effect_t s_led_mx_effect = BSP_LED_MX_EFFECT_STATIC;
static uint16_t s_led_mx_phase = 0;

// Audio
static const audio_codec_data_if_t *i2s_data_if = NULL;  /* Codec data interface */
static i2s_chan_handle_t i2s_tx_chan;
static esp_codec_dev_handle_t speaker_dev_handle = NULL;
static esp_codec_dev_handle_t microphone_dev_handle = NULL;  /* Add microphone device handle */
static bool audio_player_initialized = false;
static bool speaker_device_opened = false;
static bool microphone_device_opened = false;  /* Add microphone device state */
static uint8_t current_volume = BSP_AUDIO_VOLUME_DEFAULT;
static audio_player_cb_t audio_callback = NULL;
static void *audio_callback_user_data = NULL;

// Vibration sensor functions
static int vibration_level_change_count = 0;  
static int last_vibration_level = -1;         
static bool vibration_detected = false;   
static int g_vibration_sensitivity_threshold = 5;    

// LED strip config
static const led_strip_config_t bsp_strip_config = {
    .strip_gpio_num = BSP_RGB_CTRL,
    .max_leds = BSP_LED_STRIP_COUNT,    
    .led_model = LED_MODEL_WS2812,
    .flags.invert_out = false,
};

static const led_strip_rmt_config_t bsp_rmt_config = {
    .clk_src = RMT_CLK_SRC_DEFAULT,
    .resolution_hz = 10 * 1000 * 1000,
    .flags.with_dma = false,
};




/* Power control */
void __attribute__((constructor)) bsp_power_control_init(void)
{
    ESP_LOGW(TAG, "ESP32-C5 Sensairpanel power control auto init");

    gpio_config_t io_power_conf = {
        .pin_bit_mask = (1ULL << BSP_POWER_CTRL),
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_power_conf);

    gpio_set_level(BSP_POWER_CTRL, 1);
}

void bsp_power_control_set_power(bool power_on)
{
    gpio_set_level(BSP_POWER_CTRL, power_on ? 1 : 0);
}




/* LED */
esp_err_t bsp_led_init()
{
    ESP_LOGI(TAG, "Initializing LED strip with %d LEDs on GPIO %d", 
             BSP_LED_STRIP_COUNT, bsp_strip_config.strip_gpio_num);

    esp_err_t ret = led_strip_new_rmt_device(&bsp_strip_config, &bsp_rmt_config, &led_strip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LED strip: %s", esp_err_to_name(ret));
        return ret;
    }

    for (int i = 0; i < BSP_LED_STRIP_COUNT; i++) {
        led_configs[i].color = BSP_LED_COLOR_OFF;
        led_configs[i].brightness = BSP_LED_BRIGHTNESS_MID;
        led_configs[i].effect = BSP_LED_EFFECT_STATIC;
        led_configs[i].effect_speed = 1000;
    }

    ret = led_strip_clear(led_strip);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to clear LED strip: %s", esp_err_to_name(ret));
        led_strip_del(led_strip);
        return ret;
    }

    ESP_LOGI(TAG, "LED strip initialized successfully");
    return ESP_OK;
}

esp_err_t bsp_led_deinit()
{
    if (led_effect_timer) {
        esp_timer_stop(led_effect_timer);
        esp_timer_delete(led_effect_timer);
        led_effect_timer = NULL;
    }
    
    if (led_strip) {
        esp_err_t ret = led_strip_del(led_strip);
        led_strip = NULL;
        return ret;
    }
    
    return ESP_OK;
}

esp_err_t bsp_led_set_rgb(uint8_t index, uint8_t r, uint8_t g, uint8_t b)
{
    if (!led_strip) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = ESP_OK;

    if (index == BSP_LED_ALL_INDEX) {
        for (int i = 0; i < BSP_LED_STRIP_COUNT; i++) {
            ret |= led_strip_set_pixel(led_strip, i, r, g, b);
            led_configs[i].color = (r << 16) | (g << 8) | b;
        }
    } else if (index < BSP_LED_STRIP_COUNT) {
        ret = led_strip_set_pixel(led_strip, index, r, g, b);
        led_configs[index].color = (r << 16) | (g << 8) | b;
    } else {
        return ESP_ERR_INVALID_ARG;
    }

    if (ret == ESP_OK) {
        ret = led_strip_refresh(led_strip);
    }

    return ret;
}

esp_err_t bsp_led_set_color(uint8_t index, uint32_t color)
{
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    
    return bsp_led_set_rgb(index, r, g, b);
}

esp_err_t bsp_led_set_hsv(uint8_t index, uint16_t hue, uint8_t saturation, uint8_t value)
{
    if (!led_strip) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = ESP_OK;

    if (index == BSP_LED_ALL_INDEX) {
        for (int i = 0; i < BSP_LED_STRIP_COUNT; i++) {
            ret |= led_strip_set_pixel_hsv(led_strip, i, hue, saturation, value);
        }
    } else if (index < BSP_LED_STRIP_COUNT) {
        ret = led_strip_set_pixel_hsv(led_strip, index, hue, saturation, value);
    } else {
        return ESP_ERR_INVALID_ARG;
    }

    if (ret == ESP_OK) {
        ret = led_strip_refresh(led_strip);
    }

    return ret;
}

esp_err_t bsp_led_set_colors(const uint32_t *colors)
{
    if (!led_strip || !colors) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = ESP_OK;
    
    for (int i = 0; i < BSP_LED_STRIP_COUNT; i++) {
        uint8_t r = (colors[i] >> 16) & 0xFF;
        uint8_t g = (colors[i] >> 8) & 0xFF;
        uint8_t b = colors[i] & 0xFF;
        
        ret |= led_strip_set_pixel(led_strip, i, r, g, b);
        led_configs[i].color = colors[i];
    }

    if (ret == ESP_OK) {
        ret = led_strip_refresh(led_strip);
    }

    return ret;
}

esp_err_t bsp_led_set_all_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return bsp_led_set_rgb(BSP_LED_ALL_INDEX, r, g, b);
}

esp_err_t bsp_led_clear(uint8_t index)
{
    return bsp_led_set_rgb(index, 0, 0, 0);
}

esp_err_t bsp_led_clear_all()
{
    if (!led_strip) {
        return ESP_ERR_INVALID_STATE;
    }
    
    return led_strip_clear(led_strip);
}

static void led_effect_timer_callback(void *arg)
{
    if (!effect_running || !led_strip) {
        return;
    }

    switch (current_effect) {
        case BSP_LED_EFFECT_BREATHING: {
            float brightness = (sin(effect_step * 0.1) + 1.0) / 2.0;
            uint8_t level = (uint8_t)(brightness * 255);
            bsp_led_set_all_rgb(level, level, level);
            break;
        }
        
        case BSP_LED_EFFECT_RAINBOW: {
            for (int i = 0; i < BSP_LED_STRIP_COUNT; i++) {
                uint16_t hue = (effect_step * 10 + i * 60) % 360;
                led_strip_set_pixel_hsv(led_strip, i, hue, 255, 128);
            }
            led_strip_refresh(led_strip);
            break;
        }
        
        case BSP_LED_EFFECT_CHASE: {
            bsp_led_clear_all();
            uint8_t pos = effect_step % BSP_LED_STRIP_COUNT;
            bsp_led_set_rgb(pos, 255, 255, 255);
            break;
        }
        
        default:
            break;
    }
    
    effect_step++;
}

esp_err_t bsp_led_start_effect(bsp_led_effect_t effect, uint32_t color, uint16_t speed)
{
    if (effect >= BSP_LED_EFFECT_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    bsp_led_stop_effect();

    current_effect = effect;
    effect_step = 0;
    effect_running = true;

    esp_timer_create_args_t timer_args = {
        .callback = led_effect_timer_callback,
        .arg = NULL,
        .name = "led_effect_timer"
    };
    
    esp_err_t ret = esp_timer_create(&timer_args, &led_effect_timer);
    if (ret != ESP_OK) {
        return ret;
    }

    return esp_timer_start_periodic(led_effect_timer, speed * 1000);
}

esp_err_t bsp_led_stop_effect()
{
    effect_running = false;
    
    if (led_effect_timer) {
        esp_timer_stop(led_effect_timer);
        esp_timer_delete(led_effect_timer);
        led_effect_timer = NULL;
    }
    
    return ESP_OK;
}

esp_err_t bsp_led_rgb_set(uint8_t r, uint8_t g, uint8_t b)
{
    return bsp_led_set_all_rgb(r, g, b);
}

/* Touch-LED Synchronization Functions */

/**
 * @brief Convert touch button source to corresponding LED index
 */
static uint8_t touch_source_to_led_index(bsp_button_source_t touch_source)
{
    switch (touch_source) {
        case BSP_INPUT_TOUCH_1: return BSP_LED_INDEX_5;  // Bottom Right
        case BSP_INPUT_TOUCH_2: return BSP_LED_INDEX_4;  // Right
        case BSP_INPUT_TOUCH_3: return BSP_LED_INDEX_3;  // Top Right
        case BSP_INPUT_TOUCH_4: return BSP_LED_INDEX_2;  // Top Left
        case BSP_INPUT_TOUCH_5: return BSP_LED_INDEX_1;  // Left
        case BSP_INPUT_TOUCH_6: return BSP_LED_INDEX_0;  // Bottom Left
        default: return 0xFF;  // Invalid
    }
}

esp_err_t bsp_led_set_for_touch(bsp_button_source_t touch_source, uint32_t color)
{
    uint8_t led_index = touch_source_to_led_index(touch_source);
    if (led_index == 0xFF) {
        return ESP_ERR_INVALID_ARG;
    }
    
    return bsp_led_set_color(led_index, color);
}

esp_err_t bsp_led_set_rgb_for_touch(bsp_button_source_t touch_source, uint8_t r, uint8_t g, uint8_t b)
{
    uint8_t led_index = touch_source_to_led_index(touch_source);
    if (led_index == 0xFF) {
        return ESP_ERR_INVALID_ARG;
    }
    
    return bsp_led_set_rgb(led_index, r, g, b);
}

esp_err_t bsp_led_clear_for_touch(bsp_button_source_t touch_source)
{
    uint8_t led_index = touch_source_to_led_index(touch_source);
    if (led_index == 0xFF) {
        return ESP_ERR_INVALID_ARG;
    }
    
    return bsp_led_clear(led_index);
}

/* LED Matrix (16x16 on BSP_RGB_EXT_CTRL) */
static inline uint16_t led_mx_index(uint16_t x, uint16_t y)
{
    // serpentine mapping, row-major, origin: top-left
    bool serp = true;
    if (serp && (y % 2)) {
        return y * BSP_LED_MATRIX_COLS + (BSP_LED_MATRIX_COLS - 1 - x);
    }
    return y * BSP_LED_MATRIX_COLS + x;
}

static inline uint32_t led_mx_apply_brightness(uint32_t rgb)
{
    uint8_t r = (rgb >> 16) & 0xFF;
    uint8_t g = (rgb >> 8) & 0xFF;
    uint8_t b = rgb & 0xFF;
    r = (r * s_led_mx_brightness) / 255;
    g = (g * s_led_mx_brightness) / 255;
    b = (b * s_led_mx_brightness) / 255;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

static void led_mx_refresh_hw(void)
{
    if (!s_led_mx) return;
    for (uint16_t y = 0; y < BSP_LED_MATRIX_ROWS; ++y) {
        for (uint16_t x = 0; x < BSP_LED_MATRIX_COLS; ++x) {
            uint16_t idx = led_mx_index(x, y);
            uint32_t c = led_mx_apply_brightness(s_led_mx_fb[idx]);
            uint8_t r = (c >> 16) & 0xFF;
            uint8_t g = (c >> 8) & 0xFF;
            uint8_t b = c & 0xFF;
            led_strip_set_pixel(s_led_mx, idx, r, g, b);
        }
    }
    led_strip_refresh(s_led_mx);
}

static void led_mx_timer_cb(void *arg)
{
    if (!s_led_mx) return;
    switch (s_led_mx_effect) {
        case BSP_LED_MX_EFFECT_BREATH: {
            // global brightness breath
            float t = (s_led_mx_phase % 200) / 200.0f; // 0..1
            float s = (sinf(t * 2 * 3.1415926f) + 1.0f) * 0.5f; // 0..1
            s_led_mx_brightness = (uint8_t)(s * 255);
            break;
        }
        case BSP_LED_MX_EFFECT_RAINBOW: {
            for (uint16_t y = 0; y < BSP_LED_MATRIX_ROWS; ++y) {
                for (uint16_t x = 0; x < BSP_LED_MATRIX_COLS; ++x) {
                    uint16_t idx = led_mx_index(x, y);
                    uint16_t hue = (s_led_mx_phase * 4 + (x + y) * 8) % 360;
                    // simple HSV to RGB (sat=255, val=255)
                    uint8_t region = hue / 60;
                    uint8_t rem = (hue % 60) * 255 / 60;
                    uint8_t q = 255 - rem, t2 = rem;
                    uint8_t r,g,b;
                    switch(region){
                        case 0: r=255; g=t2; b=0; break;
                        case 1: r=q; g=255; b=0; break;
                        case 2: r=0; g=255; b=t2; break;
                        case 3: r=0; g=q; b=255; break;
                        case 4: r=t2; g=0; b=255; break;
                        default: r=255; g=0; b=q; break;
                    }
                    s_led_mx_fb[idx] = ((uint32_t)r<<16)|((uint32_t)g<<8)|b;
                }
            }
            break;
        }
        case BSP_LED_MX_EFFECT_SCAN: {
            // horizontal scan white dot
            uint16_t pos = s_led_mx_phase % BSP_LED_MATRIX_COLS;
            for (uint16_t y = 0; y < BSP_LED_MATRIX_ROWS; ++y) {
                for (uint16_t x = 0; x < BSP_LED_MATRIX_COLS; ++x) {
                    uint16_t idx = led_mx_index(x, y);
                    s_led_mx_fb[idx] = (x == pos) ? 0xFFFFFF : 0x000000;
                }
            }
            break;
        }
        default:
            break;
    }
    s_led_mx_phase++;
    led_mx_refresh_hw();
}

esp_err_t bsp_led_matrix_init(void)
{
    if (s_led_mx) return ESP_OK;
    led_strip_config_t cfg = {
        .strip_gpio_num = BSP_RGB_EXT_CTRL,
        .max_leds = BSP_LED_MATRIX_ROWS * BSP_LED_MATRIX_COLS,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };
    led_strip_rmt_config_t rmt = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };
    esp_err_t e = led_strip_new_rmt_device(&cfg, &rmt, &s_led_mx);
    if (e != ESP_OK) return e;
    for (size_t i = 0; i < BSP_LED_MATRIX_ROWS * BSP_LED_MATRIX_COLS; ++i) s_led_mx_fb[i] = 0;
    led_strip_clear(s_led_mx);

    esp_timer_create_args_t targs = {
        .callback = led_mx_timer_cb,
        .arg = NULL,
        .name = "led_mx",
    };
    ESP_ERROR_CHECK(esp_timer_create(&targs, &s_led_mx_timer));
    ESP_LOGI(TAG, "LED matrix initialized: %ux%u on GPIO %d", BSP_LED_MATRIX_COLS, BSP_LED_MATRIX_ROWS, BSP_RGB_EXT_CTRL);
    return ESP_OK;
}

esp_err_t bsp_led_matrix_deinit(void)
{
    if (s_led_mx_timer) {
        esp_timer_stop(s_led_mx_timer);
        esp_timer_delete(s_led_mx_timer);
        s_led_mx_timer = NULL;
    }
    if (s_led_mx) {
        led_strip_del(s_led_mx);
        s_led_mx = NULL;
    }
    return ESP_OK;
}

esp_err_t bsp_led_matrix_set_pixel(uint16_t x, uint16_t y, uint32_t rgb)
{
    if (x >= BSP_LED_MATRIX_COLS || y >= BSP_LED_MATRIX_ROWS) return ESP_ERR_INVALID_ARG;
    s_led_mx_fb[led_mx_index(x,y)] = rgb;
    return ESP_OK;
}

esp_err_t bsp_led_matrix_fill(uint32_t rgb)
{
    for (size_t i = 0; i < BSP_LED_MATRIX_ROWS * BSP_LED_MATRIX_COLS; ++i) s_led_mx_fb[i] = rgb;
    return ESP_OK;
}

esp_err_t bsp_led_matrix_clear(void)
{
    return bsp_led_matrix_fill(0);
}

esp_err_t bsp_led_matrix_refresh(void)
{
    if (!s_led_mx) return ESP_ERR_INVALID_STATE;
    led_mx_refresh_hw();
    return ESP_OK;
}

esp_err_t bsp_led_matrix_set_brightness(uint8_t level)
{
    s_led_mx_brightness = level;
    return ESP_OK;
}

esp_err_t bsp_led_matrix_start_effect(bsp_led_matrix_effect_t effect, uint32_t color, uint16_t speed_ms)
{
    if (!s_led_mx || !s_led_mx_timer) return ESP_ERR_INVALID_STATE;
    s_led_mx_effect = effect;
    s_led_mx_phase = 0;
    if (effect == BSP_LED_MX_EFFECT_STATIC) {
        // fill with color and stop timer
        bsp_led_matrix_fill(color);
        led_mx_refresh_hw();
        esp_timer_stop(s_led_mx_timer);
        return ESP_OK;
    }
    esp_timer_stop(s_led_mx_timer);
    return esp_timer_start_periodic(s_led_mx_timer, (uint64_t)speed_ms * 1000ULL);
}

esp_err_t bsp_led_matrix_stop_effect(void)
{
    if (!s_led_mx_timer) return ESP_OK;
    esp_timer_stop(s_led_mx_timer);
    return ESP_OK;
}




/* Audio */
esp_codec_dev_handle_t bsp_audio_codec_speaker_init(void)
{
    if (speaker_dev_handle) {
        return speaker_dev_handle;
    }
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    BSP_ERROR_CHECK_RETURN_NULL(i2s_new_channel(&chan_cfg, &i2s_tx_chan, NULL));

    i2s_pdm_tx_config_t pdm_cfg = BSP_I2S_DUPLEX_MONO_CFG(BSP_OUTPUT_SAMPLE_RATE, BSP_PDM_SPEAK_P_GPIO);
    pdm_cfg.clk_cfg.up_sample_fs = BSP_OUTPUT_SAMPLE_RATE / 100;
    pdm_cfg.slot_cfg.sd_scale = I2S_PDM_SIG_SCALING_MUL_4;
    pdm_cfg.slot_cfg.hp_scale = I2S_PDM_SIG_SCALING_MUL_4;
    pdm_cfg.slot_cfg.lp_scale = I2S_PDM_SIG_SCALING_MUL_4;
    pdm_cfg.slot_cfg.sinc_scale = I2S_PDM_SIG_SCALING_MUL_4;

    BSP_ERROR_CHECK_RETURN_NULL(i2s_channel_init_pdm_tx_mode(i2s_tx_chan, &pdm_cfg));

    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = I2S_NUM_0,
        .rx_handle = NULL,
        .tx_handle = i2s_tx_chan,
    };

    i2s_data_if = audio_codec_new_i2s_data(&i2s_cfg);
    BSP_ERROR_CHECK_RETURN_NULL(i2s_channel_enable(i2s_tx_chan));

    if (BSP_PA_CTL_GPIO != GPIO_NUM_NC) {
        gpio_config_t io_conf = {
            .intr_type = GPIO_INTR_DISABLE,
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = (1ULL << BSP_PA_CTL_GPIO),
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .pull_up_en = GPIO_PULLUP_DISABLE,
        };
        gpio_config(&io_conf);
    }
    gpio_set_drive_capability(BSP_PDM_SPEAK_P_GPIO, GPIO_DRIVE_CAP_0);

    if(BSP_PDM_SPEAK_N_GPIO != GPIO_NUM_NC){
        // ESP_LOGI(TAG, "BSP_PDM_SPEAK_N_GPIO: %d", BSP_PDM_SPEAK_N_GPIO);
        // gpio_config_t io_conf_n = {
        //     .intr_type = GPIO_INTR_DISABLE,
        //     .mode = GPIO_MODE_OUTPUT,
        //     .pin_bit_mask = (1ULL << BSP_PDM_SPEAK_N_GPIO),
        //     .pull_down_en = GPIO_PULLDOWN_DISABLE,
        //     .pull_up_en = GPIO_PULLUP_DISABLE,
        // };
        // gpio_config(&io_conf_n);
        PIN_FUNC_SELECT(IO_MUX_GPIO4_REG, PIN_FUNC_GPIO);
        gpio_set_direction(BSP_PDM_SPEAK_N_GPIO, GPIO_MODE_OUTPUT);
        esp_rom_gpio_connect_out_signal(BSP_PDM_SPEAK_N_GPIO, I2SO_SD_OUT_IDX, 1, 0); //反转输出 SD OUT 信号
        gpio_set_drive_capability(BSP_PDM_SPEAK_N_GPIO, GPIO_DRIVE_CAP_0);
    }

    esp_codec_dev_cfg_t codec_dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .data_if = i2s_data_if,
        .codec_if = NULL,
    };
    speaker_dev_handle = esp_codec_dev_new(&codec_dev_cfg);
    return speaker_dev_handle;
}

esp_codec_dev_handle_t bsp_audio_codec_microphone_init(void)
{
    audio_codec_adc_cfg_t cfg = DEFAULT_AUDIO_CODEC_ADC_MONO_CFG(BSP_ADC_MIC_CHANNEL, BSP_INPUT_SAMPLE_RATE);
    const audio_codec_data_if_t *adc_if = audio_codec_new_adc_data(&cfg);

    esp_codec_dev_cfg_t codec_dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
        .data_if = adc_if,
    };

    return esp_codec_dev_new(&codec_dev_cfg);
}

static esp_err_t bsp_audio_mute_function(AUDIO_PLAYER_MUTE_SETTING setting)
{
    esp_err_t ret = ESP_OK;
    
    if (speaker_dev_handle && speaker_device_opened) {
        ret = esp_codec_dev_set_out_mute(speaker_dev_handle, setting == AUDIO_PLAYER_MUTE);
    }
    
    if (BSP_PA_CTL_GPIO != GPIO_NUM_NC) {
        gpio_set_level(BSP_PA_CTL_GPIO, setting == AUDIO_PLAYER_MUTE ? 0 : 1);
    }
    
    return ret;
}

esp_err_t bsp_audio_read(void *buffer, size_t len, size_t *bytes_read, uint32_t timeout_ms)
{
    if (!buffer || !bytes_read) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Initialize microphone device if not already done
    if (!microphone_dev_handle) {
        microphone_dev_handle = bsp_audio_codec_microphone_init();
        if (!microphone_dev_handle) {
            *bytes_read = 0;
            return ESP_ERR_INVALID_STATE;
        }
    }
    
    // Open microphone device if not already opened
    if (!microphone_device_opened) {
        esp_codec_dev_sample_info_t fs = {
            .sample_rate = BSP_INPUT_SAMPLE_RATE,
            .channel = 1,
            .bits_per_sample = 16,
        };
        
        esp_err_t ret = esp_codec_dev_open(microphone_dev_handle, &fs);
        if (ret != ESP_OK) {
            *bytes_read = 0;
            return ret;
        }
        microphone_device_opened = true;
    }
    
    // Read audio data from microphone
    esp_err_t ret = esp_codec_dev_read(microphone_dev_handle, buffer, len);
    if (ret == ESP_OK) {
        *bytes_read = len;
    } else {
        *bytes_read = 0;
    }
    
    return ret;
}

static esp_err_t bsp_audio_write_function(void *buffer, size_t len, size_t *bytes_written, uint32_t timeout_ms)
{
    if (!speaker_dev_handle || !speaker_device_opened) {
        *bytes_written = 0;
        return ESP_FAIL;
    }
    
    esp_err_t ret = esp_codec_dev_write(speaker_dev_handle, buffer, len);
    if (ret == ESP_OK) {
        *bytes_written = len;
    } else {
        *bytes_written = 0;
    }
    return ret;
}

static esp_err_t bsp_audio_reconfig_clock(uint32_t rate, uint32_t bits_cfg, i2s_slot_mode_t ch)
{
    if (!speaker_dev_handle) return ESP_FAIL;
    
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = rate,
        .channel = ch,
        .bits_per_sample = bits_cfg,
    };
    
    if (speaker_device_opened) {
        esp_codec_dev_close(speaker_dev_handle);
        speaker_device_opened = false;
    }
    
    esp_err_t ret = esp_codec_dev_open(speaker_dev_handle, &fs);
    if (ret == ESP_OK) {
        speaker_device_opened = true;
        bsp_audio_set_volume(current_volume);
        if (BSP_PA_CTL_GPIO != GPIO_NUM_NC) {
            gpio_set_level(BSP_PA_CTL_GPIO, 1);
        }
    }
    return ret;
}

static void bsp_audio_player_callback(audio_player_cb_ctx_t *ctx)
{
    if (audio_callback) {
        ctx->user_ctx = audio_callback_user_data;
        audio_callback(ctx);
    }
}

esp_err_t bsp_audio_player_init(void)
{
    if (audio_player_initialized) return ESP_OK;
    
    if (!speaker_dev_handle) {
        speaker_dev_handle = bsp_audio_codec_speaker_init();
        if (!speaker_dev_handle) return ESP_FAIL;
    }
    
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = BSP_OUTPUT_SAMPLE_RATE,
        .channel = 1,
        .bits_per_sample = 16,
    };
    
    esp_err_t ret = esp_codec_dev_open(speaker_dev_handle, &fs);
    if (ret == ESP_OK) {
        speaker_device_opened = true;
        bsp_audio_set_volume(current_volume);
        if (BSP_PA_CTL_GPIO != GPIO_NUM_NC) {
            gpio_set_level(BSP_PA_CTL_GPIO, 1);
        }
    } else {
        return ret;
    }
    
    audio_player_config_t config = {
        .mute_fn = bsp_audio_mute_function,
        .write_fn = bsp_audio_write_function,
        .clk_set_fn = bsp_audio_reconfig_clock,
        .priority = 5,
    };
    
    ret = audio_player_new(config);
    if (ret == ESP_OK) {
        audio_player_callback_register(bsp_audio_player_callback, NULL);
        audio_player_initialized = true;
    }
    return ret;
}

esp_err_t bsp_audio_player_deinit(void)
{
    if (!audio_player_initialized) return ESP_OK;
    
    esp_err_t ret = audio_player_delete();
    
    if (speaker_device_opened && speaker_dev_handle) {
        esp_codec_dev_close(speaker_dev_handle);
        speaker_device_opened = false;
    }
    
    if (BSP_PA_CTL_GPIO != GPIO_NUM_NC) {
        gpio_set_level(BSP_PA_CTL_GPIO, 0);
    }
    
    audio_player_initialized = false;
    return ret;
}

esp_err_t bsp_audio_file_iterator_init(const char *path, file_iterator_instance_t **instance)
{
    if (!path || !instance) return ESP_ERR_INVALID_ARG;
    
    file_iterator_instance_t *iter = file_iterator_new(path);
    if (!iter) return ESP_FAIL;
    
    *instance = iter;
    return ESP_OK;
}

esp_err_t bsp_audio_play_file(const char *file_path)
{
    if (!file_path) return ESP_ERR_INVALID_ARG;
    
    FILE *fp = fopen(file_path, "rb");
    if (!fp) return ESP_FAIL;
    
    return audio_player_play(fp);
}

esp_err_t bsp_audio_play_index(file_iterator_instance_t *instance, int index)
{
    if (!instance) return ESP_ERR_INVALID_ARG;
    
    char filename[128];
    if (file_iterator_get_full_path_from_index(instance, index, filename, sizeof(filename)) == 0) {
        return ESP_FAIL;
    }
    
    FILE *fp = fopen(filename, "rb");
    if (!fp) return ESP_FAIL;
    
    return audio_player_play(fp);
}

void bsp_audio_register_callback(audio_player_cb_t cb, void *user_data)
{
    audio_callback = cb;
    audio_callback_user_data = user_data;
}

esp_err_t bsp_audio_set_volume(uint8_t volume)
{
    if (volume < BSP_AUDIO_VOLUME_MIN) {
        volume = BSP_AUDIO_VOLUME_MIN;
    } else if (volume > BSP_AUDIO_VOLUME_MAX) {
        volume = BSP_AUDIO_VOLUME_MAX;
    }
    
    current_volume = volume;
    
    if (speaker_dev_handle && speaker_device_opened) {
        return esp_codec_dev_set_out_vol(speaker_dev_handle, volume);
    }
    
    return ESP_OK;
}

esp_err_t bsp_audio_get_volume(uint8_t *volume)
{
    if (!volume) return ESP_ERR_INVALID_ARG;
    
    *volume = current_volume;
    return ESP_OK;
}




/* SPIFFS */
esp_err_t bsp_spiffs_mount(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = CONFIG_BSP_SPIFFS_MOUNT_POINT,
        .partition_label = CONFIG_BSP_SPIFFS_PARTITION_LABEL,
        .max_files = CONFIG_BSP_SPIFFS_MAX_FILES,
#ifdef CONFIG_BSP_SPIFFS_FORMAT_ON_MOUNT_FAIL
        .format_if_mount_failed = true,
#else
        .format_if_mount_failed = false,
#endif
    };

    esp_err_t ret_val = esp_vfs_spiffs_register(&conf);

    BSP_ERROR_CHECK_RETURN_ERR(ret_val);

    size_t total = 0, used = 0;
    ret_val = esp_spiffs_info(conf.partition_label, &total, &used);
    if (ret_val != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret_val));
    } else {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }

    return ret_val;
}

esp_err_t bsp_spiffs_unmount(void)
{
    return esp_vfs_spiffs_unregister(CONFIG_BSP_SPIFFS_PARTITION_LABEL);
}




/* Display */
static lv_display_t *bsp_display_lcd_init(const bsp_display_cfg_t *cfg)
{
    assert(cfg != NULL);
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_handle_t panel_handle = NULL;
    const bsp_display_config_t bsp_disp_cfg = {
        .max_transfer_sz = BSP_LCD_H_RES * 10 * sizeof(uint16_t),
    };
    BSP_ERROR_CHECK_RETURN_NULL(bsp_display_new(&bsp_disp_cfg, &panel_handle, &io_handle));

    /* Add LCD screen */
    ESP_LOGD(TAG, "Add LCD screen");
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = panel_handle,
        .buffer_size = cfg->buffer_size,
        .double_buffer = cfg->double_buffer,
        .hres = BSP_LCD_H_RES,
        .vres = BSP_LCD_V_RES,
        .monochrome = false,
        /* Rotation values must be same as used in esp_lcd for initial settings of the screen */
        .rotation = {
            .swap_xy = false,
            .mirror_x = true,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma = cfg->flags.buff_dma,
            .buff_spiram = cfg->flags.buff_spiram,
#if LVGL_VERSION_MAJOR >= 9
            .swap_bytes = (BSP_LCD_BIGENDIAN ? true : false),
#endif
        }
    };

    return lvgl_port_add_disp(&disp_cfg);
}

// Bit number used to represent command and parameter
#define LCD_CMD_BITS           8
#define LCD_PARAM_BITS         8

esp_err_t bsp_display_brightness_init(void)
{
    // Configure backlight GPIO as output and default OFF
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << BSP_LCD_BACKLIGHT),
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io), TAG, "Backlight GPIO config failed");
    gpio_set_level(BSP_LCD_BACKLIGHT, 0);
    return ESP_OK;
}

esp_err_t bsp_display_brightness_set(int brightness_percent)
{
    // Any positive value turns on, zero or negative turns off
    int level = (brightness_percent > 0) ? 1 : 0;
    gpio_set_level(BSP_LCD_BACKLIGHT, level);
    return ESP_OK;
}

esp_err_t bsp_display_backlight_off(void)
{
    return bsp_display_brightness_set(0);
}

esp_err_t bsp_display_backlight_on(void)
{
    return bsp_display_brightness_set(1);
}

esp_err_t bsp_display_new(const bsp_display_config_t *config, esp_lcd_panel_handle_t *ret_panel, esp_lcd_panel_io_handle_t *ret_io)
{
    esp_err_t ret = ESP_OK;
    assert(config != NULL && config->max_transfer_sz > 0);

    ESP_RETURN_ON_ERROR(bsp_display_brightness_init(), TAG, "Brightness init failed");

    ESP_LOGD(TAG, "Initialize SPI bus");
    const spi_bus_config_t buscfg = {
        .sclk_io_num = BSP_LCD_PCLK,
        .mosi_io_num = BSP_LCD_DATA0,
        .miso_io_num = GPIO_NUM_NC,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = config->max_transfer_sz,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(BSP_LCD_SPI_NUM, &buscfg, SPI_DMA_CH_AUTO), TAG, "SPI init failed");

    ESP_LOGD(TAG, "Install panel IO");
    const esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = BSP_LCD_DC,
        // .cs_gpio_num = BSP_LCD_CS,
        .pclk_hz = BSP_LCD_PIXEL_CLOCK_HZ,
        .lcd_cmd_bits = LCD_CMD_BITS,
        .lcd_param_bits = LCD_PARAM_BITS,
        .spi_mode = 0,
        .trans_queue_depth = 2,
    };
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BSP_LCD_SPI_NUM, &io_config, ret_io), err, TAG, "New panel IO failed");

    ESP_LOGD(TAG, "Install LCD driver");
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = BSP_LCD_RST, // Shared with Touch reset
        .color_space = BSP_LCD_COLOR_SPACE,
        .bits_per_pixel = BSP_LCD_BITS_PER_PIXEL,
    };
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_gc9a01(*ret_io, &panel_config, ret_panel), err, TAG, "New panel failed");

    BSP_ERROR_CHECK_RETURN_ERR(esp_lcd_panel_reset(*ret_panel));
    BSP_ERROR_CHECK_RETURN_ERR(esp_lcd_panel_init(*ret_panel));
    BSP_ERROR_CHECK_RETURN_ERR(esp_lcd_panel_invert_color(*ret_panel, true));
    BSP_ERROR_CHECK_RETURN_ERR(esp_lcd_panel_mirror(*ret_panel, true, false));
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    BSP_ERROR_CHECK_RETURN_ERR(esp_lcd_panel_disp_on_off(*ret_panel, true));
#else
    BSP_ERROR_CHECK_RETURN_ERR(esp_lcd_panel_disp_off(*ret_panel, false));
#endif

    return ret;

err:
    if (*ret_panel) {
        esp_lcd_panel_del(*ret_panel);
    }
    if (*ret_io) {
        esp_lcd_panel_io_del(*ret_io);
    }
    spi_bus_free(BSP_LCD_SPI_NUM);
    return ret;
}

lv_display_t *bsp_display_start(void)
{
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_H_RES * BSP_LCD_V_RES,
#if CONFIG_BSP_LCD_DRAW_BUF_DOUBLE
        .double_buffer = 1,
#else
        .double_buffer = 0,
#endif
        .flags = {
            .buff_dma = false,
            .buff_spiram = true,
        }
    };
    return bsp_display_start_with_config(&cfg);
}

lv_disp_t *bsp_display_start_with_config(const bsp_display_cfg_t *cfg)
{
    lv_disp_t *disp;
    BSP_ERROR_CHECK_RETURN_NULL(lvgl_port_init(&cfg->lvgl_port_cfg));
    BSP_ERROR_CHECK_RETURN_NULL(bsp_display_brightness_init());
    BSP_NULL_CHECK(disp = bsp_display_lcd_init(cfg), NULL);

    return disp;
}

void bsp_display_rotate(lv_display_t *disp, lv_disp_rotation_t rotation)
{
    lv_disp_set_rotation(disp, rotation);
}

bool bsp_display_lock(uint32_t timeout_ms)
{
    return lvgl_port_lock(timeout_ms);
}

void bsp_display_unlock(void)
{
    lvgl_port_unlock();
}




/* I2C Bus Management */
esp_err_t bsp_i2c_init(const bsp_i2c_config_t *config)
{
    if (!config) {
        ESP_LOGE(TAG, "Invalid I2C configuration");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (s_i2c_bus_handle != NULL) {
        ESP_LOGW(TAG, "I2C bus already initialized");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing I2C bus on SDA=%d, SCL=%d", config->sda_io_num, config->scl_io_num);
    
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = config->sda_io_num,
        .scl_io_num = config->scl_io_num,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .intr_priority = 0,
    };
    
    esp_err_t ret = i2c_new_master_bus(&bus_config, &s_i2c_bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2C bus: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "I2C bus initialized successfully");

    /* Debug probe: verify BS8112A3 is responding on the bus before continuing */
    esp_err_t probe_ret = i2c_master_probe(s_i2c_bus_handle, BS8112A3_I2C_ADDR, 1000);
    if (probe_ret == ESP_OK) {
        ESP_LOGI(TAG, "I2C probe 0x%02X success", BS8112A3_I2C_ADDR);
    } else {
        ESP_LOGE(TAG, "I2C probe 0x%02X failed: %s", BS8112A3_I2C_ADDR, esp_err_to_name(probe_ret));
    }

    return ESP_OK;
}

i2c_master_bus_handle_t bsp_i2c_get_bus_handle(void)
{
    return s_i2c_bus_handle;
}

esp_err_t bsp_i2c_deinit(void)
{
    if (s_i2c_bus_handle) {
        ESP_LOGI(TAG, "Deinitializing I2C bus");
        esp_err_t ret = i2c_del_master_bus(s_i2c_bus_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to deinitialize I2C bus: %s", esp_err_to_name(ret));
        }
        s_i2c_bus_handle = NULL;
        return ret;
    }
    return ESP_OK;
}




/* Touch Buttons (BS8112A3) - Internal Functions */
/**
 * @brief Custom button initialization callback for individual touch buttons
 */
static esp_err_t bsp_touch_button_custom_init(void *param)
{
    uint32_t bit_position = (uint32_t)param;
    
    if (bit_position < 2 || bit_position > 7) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!touch_ic_bs8112a3_is_initialized()) {
        return ESP_ERR_INVALID_STATE;
    }
    
    return ESP_OK;
}

/**
 * @brief Custom button deinitialization callback for individual touch buttons
 */
static esp_err_t bsp_touch_button_custom_deinit(void *param)
{
    uint32_t bit_position = (uint32_t)param;
    
    if (bit_position < 2 || bit_position > 7) {
        return ESP_ERR_INVALID_ARG;
    }
    
    return ESP_OK;
}

/**
 * @brief Check if touch hardware is available and functional
 */
bool bsp_touch_hardware_available(void)
{
    return touch_hardware_available;
}

/**
 * @brief Custom button get key value function for touch buttons
 */
static uint8_t bsp_touch_custom_get_key_value(void *param)
{
    uint32_t bit_position = (uint32_t)param;
    
    if (!touch_hardware_available || bit_position < 2 || bit_position > 7) {
        return 0;
    }
    
    uint32_t touch_index = bit_position - 2;
    if (touch_index >= TOUCH_BUTTON_NUM) {
        return 0;
    }
    
    return touch_ic_bs8112a3_get_key_value(touch_index) ? 1 : 0;
}

/**
 * @brief Internal function to initialize touch buttons
 * 
 * Touch Button Physical Layout:
 *          4 (Top Left)    3 (Top Right)
 *                     \   /
 *                      \ /
 *       5 (Left) -------o------- 2 (Right)
 *                      / \
 *                     /   \
 *          6 (Bottom Left) 1 (Bottom Right)
 * 
 * LED-Touch Button Mapping:
 * - Touch Button 1 (Bottom Right) <-> LED 0 (Bottom Right)
 * - Touch Button 2 (Right)        <-> LED 1 (Right)
 * - Touch Button 3 (Top Right)    <-> LED 2 (Top Right)
 * - Touch Button 4 (Top Left)     <-> LED 3 (Top Left)
 * - Touch Button 5 (Left)         <-> LED 4 (Left)
 * - Touch Button 6 (Bottom Left)  <-> LED 5 (Bottom Left)
 */
typedef struct {
    button_driver_t base;
    uint32_t bit_position;
} bs8112_button_driver_t;

static uint8_t bs8112_button_get_key_level(button_driver_t *driver)
{
    bs8112_button_driver_t *d = (bs8112_button_driver_t *)driver;
    return bsp_touch_custom_get_key_value((void *)d->bit_position);
}

static esp_err_t bs8112_button_del(button_driver_t *driver)
{
    free(driver);
    return ESP_OK;
}

static button_driver_t *bs8112_new_driver(uint32_t bit_position)
{
    bs8112_button_driver_t *drv = (bs8112_button_driver_t *)calloc(1, sizeof(bs8112_button_driver_t));
    if (!drv) {
        return NULL;
    }
    drv->base.enable_power_save = false;
    drv->base.get_key_level = bs8112_button_get_key_level;
    drv->base.enter_power_save = NULL;
    drv->base.del = bs8112_button_del;
    drv->bit_position = bit_position;
    return &drv->base;
}

static void bsp_button_event_handler(void *button_handle, void *usr_data);

static esp_err_t bsp_touch_button_init_internal(void)
{
    if (!s_i2c_bus_handle) {
        ESP_LOGE(TAG, "I2C bus not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Initializing touch buttons with interrupt mode");
    
    // Initialize touch IC with interrupt mode (using BSP_TOUCH_INT pin)
    // GPIO ISR service installation is now handled inside touch_ic_bs8112a3_init()
    touch_ic_bs8112a3_config_t touch_config = {
        .i2c_bus_handle = s_i2c_bus_handle,
        .device_address = BS8112A3_I2C_ADDR,
        .scl_speed_hz = BSP_I2C_CLK_SPEED,
        .interrupt_pin = BSP_TOUCH_INT,  // Enable interrupt mode
    };
    
    esp_err_t ret = touch_ic_bs8112a3_init(&touch_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize touch IC");
        return ret;
    }
    
    button_config_t btn_cfg = {
        .long_press_time = 1000,
        .short_press_time = 180,
    };
    
    uint8_t success_count = 0;
    
    for (int i = 0; i < TOUCH_BUTTON_NUM; i++) {
        uint32_t bit_position = (uint32_t)(i + 2);
        bsp_button_source_t source = (bsp_button_source_t)(BSP_INPUT_TOUCH_1 + i);

        button_driver_t *driver = bs8112_new_driver(bit_position);
        if (!driver) {
            continue;
        }

        button_handle_t btn_handle = NULL;
        esp_err_t cret = iot_button_create(&btn_cfg, driver, &btn_handle);
        if (cret != ESP_OK) {
            if (driver && driver->del) {
                driver->del(driver);
            }
            continue;
        }

        bsp_button_handles[source] = btn_handle;
        iot_button_register_cb(btn_handle, BUTTON_PRESS_DOWN, NULL, bsp_button_event_handler, (void *)source);
        iot_button_register_cb(btn_handle, BUTTON_PRESS_UP, NULL, bsp_button_event_handler, (void *)source);
        iot_button_register_cb(btn_handle, BUTTON_SINGLE_CLICK, NULL, bsp_button_event_handler, (void *)source);
        iot_button_register_cb(btn_handle, BUTTON_LONG_PRESS_START, NULL, bsp_button_event_handler, (void *)source);
        iot_button_register_cb(btn_handle, BUTTON_DOUBLE_CLICK, NULL, bsp_button_event_handler, (void *)source);

        success_count++;
    }
    
    if (success_count == 0) {
        ESP_LOGE(TAG, "No touch buttons registered successfully");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Touch buttons initialized: %d/%d functional (interrupt mode)", success_count, TOUCH_BUTTON_NUM);
    return ESP_OK;
}

/**
 * @brief Internal function to deinitialize touch buttons
 */
static esp_err_t bsp_touch_button_deinit_internal(void)
{
    // Unregister touch buttons
    for (int i = 0; i < TOUCH_BUTTON_NUM; i++) {
        bsp_button_source_t source = BSP_INPUT_TOUCH_1 + i;
        if (bsp_button_handles[source] != NULL) {
            iot_button_delete(bsp_button_handles[source]);
            bsp_button_handles[source] = NULL;
        }
    }
    
    // Deinitialize touch IC
    esp_err_t ret = touch_ic_bs8112a3_deinit();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to deinitialize touch IC");
    }
    
    return ret;
}

/* Button Management */
static void bsp_button_event_handler(void *button_handle, void *usr_data)
{
    bsp_button_source_t source = (bsp_button_source_t)usr_data;
    button_event_t btn_event = iot_button_get_event(button_handle);
    
    bsp_button_event_t bsp_event;
    switch (btn_event) {
        case BUTTON_PRESS_DOWN:      bsp_event = BSP_BUTTON_EVENT_PRESS_DOWN; break;
        case BUTTON_PRESS_UP:        bsp_event = BSP_BUTTON_EVENT_PRESS_UP; break;
        case BUTTON_SINGLE_CLICK:    bsp_event = BSP_BUTTON_EVENT_SHORT_PRESS; break;
        case BUTTON_LONG_PRESS_START: bsp_event = BSP_BUTTON_EVENT_LONG_PRESS; break;
        case BUTTON_DOUBLE_CLICK:    bsp_event = BSP_BUTTON_EVENT_DOUBLE_CLICK; break;
        default: return;
    }
    
    if (global_button_callback) {
        global_button_callback(source, bsp_event, usr_data);
    }
}

esp_err_t bsp_button_init(bsp_button_callback_t callback)
{
    if (!callback) {
        return ESP_ERR_INVALID_ARG;
    }
    
    global_button_callback = callback;
    ESP_LOGI(TAG, "Initializing button management system");
    
    // Initialize I2C bus if not already initialized
    if (s_i2c_bus_handle == NULL) {
        bsp_i2c_config_t i2c_config = {
            .sda_io_num = BSP_I2C_SDA,
            .scl_io_num = BSP_I2C_SCL,
            .clk_speed = BSP_I2C_CLK_SPEED,
        };
        
        esp_err_t ret = bsp_i2c_init(&i2c_config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize I2C bus");
            return ret;
        }
    }
    
    // Try to initialize touch buttons
    esp_err_t ret = bsp_touch_button_init_internal();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Touch hardware not available, continuing without touch");
        touch_hardware_available = false;
    } else {
        touch_hardware_available = true;
        ESP_LOGI(TAG, "Touch hardware initialized successfully");
    }
    
    // Try to initialize light sensor
    ret = bsp_light_sensor_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Light sensor not available, continuing without light sensor");
    }
    
    ESP_LOGI(TAG, "Button management system initialized (touch: %s, light sensor: %s)", 
             touch_hardware_available ? "enabled" : "disabled",
             light_sensor_available ? "enabled" : "disabled");
    return ESP_OK;
}

esp_err_t bsp_button_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing button management system");
    
    // Deinitialize touch buttons
    esp_err_t ret = bsp_touch_button_deinit_internal();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to deinitialize touch buttons");
    }
    
    // Clean up remaining button handles
    for (int i = 0; i < BSP_INPUT_MAX; i++) {
        if (bsp_button_handles[i] != NULL) {
            iot_button_delete(bsp_button_handles[i]);
            bsp_button_handles[i] = NULL;
        }
    }
    
    // Deinitialize light sensor
    esp_err_t light_ret = bsp_light_sensor_deinit();
    if (light_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to deinitialize light sensor");
        if (ret == ESP_OK) {
            ret = light_ret;
        }
    }
    
    // Deinitialize I2C bus
    esp_err_t i2c_ret = bsp_i2c_deinit();
    if (i2c_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to deinitialize I2C bus");
        if (ret == ESP_OK) {
            ret = i2c_ret;
        }
    }
    
    global_button_callback = NULL;
    touch_hardware_available = false;
    return ret;
}

esp_err_t bsp_button_register_source(bsp_button_source_t source, const button_config_t *config)
{
    if (source >= BSP_INPUT_MAX || bsp_button_handles[source] != NULL || config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    button_driver_t *driver = NULL;
    if (source >= BSP_INPUT_TOUCH_1 && source <= BSP_INPUT_TOUCH_6) {
        uint32_t bit_position = (uint32_t)(2 + (source - BSP_INPUT_TOUCH_1));
        driver = bs8112_new_driver(bit_position);
    } else {
        driver = NULL;
    }

    if (!driver) {
        return ESP_ERR_NO_MEM;
    }

    button_handle_t handle = NULL;
    esp_err_t ret = iot_button_create(config, driver, &handle);
    if (ret != ESP_OK) {
        if (driver->del) {
            driver->del(driver);
        }
        return ret;
    }

    bsp_button_handles[source] = handle;
    iot_button_register_cb(handle, BUTTON_PRESS_DOWN, NULL, bsp_button_event_handler, (void *)source);
    iot_button_register_cb(handle, BUTTON_PRESS_UP, NULL, bsp_button_event_handler, (void *)source);
    iot_button_register_cb(handle, BUTTON_SINGLE_CLICK, NULL, bsp_button_event_handler, (void *)source);
    iot_button_register_cb(handle, BUTTON_LONG_PRESS_START, NULL, bsp_button_event_handler, (void *)source);
    iot_button_register_cb(handle, BUTTON_DOUBLE_CLICK, NULL, bsp_button_event_handler, (void *)source);
    return ESP_OK;
}




/* Light Sensor (RPR-0521) Management */
esp_err_t bsp_light_sensor_init(void)
{
    if (!s_i2c_bus_handle) {
        return ESP_ERR_INVALID_STATE;
    }
    
    light_sensor_rpr0521_config_t config = {
        .i2c_bus_handle = s_i2c_bus_handle,
        .device_address = BSP_LIGHT_SENSOR_I2C_ADDR,
        .scl_speed_hz = BSP_I2C_CLK_SPEED,
        .interrupt_pin = GPIO_NUM_NC,  // No interrupt support for now
    };
    
    esp_err_t ret = light_sensor_rpr0521_init(&config);
    if (ret != ESP_OK) {
        light_sensor_available = false;
        return ret;
    }
    
    light_sensor_available = true;
    return ESP_OK;
}

esp_err_t bsp_light_sensor_read_data0(uint16_t *light_value)
{
    if (!light_sensor_available) {
        return ESP_ERR_INVALID_STATE;
    }
    
    return light_sensor_rpr0521_read_als_data0(light_value);
}

esp_err_t bsp_light_sensor_read_data1(uint16_t *light_value)
{
    if (!light_sensor_available) {
        return ESP_ERR_INVALID_STATE;
    }
    
    return light_sensor_rpr0521_read_als_data1(light_value);
}

esp_err_t bsp_light_sensor_read_proximity(uint16_t *proximity_value)
{
    if (!light_sensor_available) {
        return ESP_ERR_INVALID_STATE;
    }
    
    return light_sensor_rpr0521_read_ps(proximity_value);
}

esp_err_t bsp_light_sensor_set_gain(uint8_t gain_level)
{
    if (!light_sensor_available || gain_level > 3) {
        return ESP_ERR_INVALID_ARG;
    }
    
    rpr0521_als_gain_t gain = (rpr0521_als_gain_t)gain_level;
    return light_sensor_rpr0521_set_als_gain(gain, gain);  // Set same gain for both DATA0 and DATA1
}

esp_err_t bsp_light_sensor_enable_proximity(bool enable)
{
    if (!light_sensor_available) {
        return ESP_ERR_INVALID_STATE;
    }
    
    return light_sensor_rpr0521_enable_ps(enable);
}

esp_err_t bsp_light_sensor_deinit(void)
{
    esp_err_t ret = light_sensor_rpr0521_deinit();
    light_sensor_available = false;
    return ret;
}

bool bsp_light_sensor_available(void)
{
    return light_sensor_available;
}




/* Infrared control state */
static bool g_ir_initialized = false;

esp_err_t bsp_ir_init(void)
{
    if (g_ir_initialized) {
        ESP_LOGW(TAG, "IR system already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "Initializing IR on GPIO %d", BSP_IR_TX_GPIO);
    ir_tx_config_t tx = {
        .tx_gpio = BSP_IR_TX_GPIO,
        .carrier_freq_hz = BSP_IR_CARRIER_FREQ,
        .resolution_hz = BSP_IR_RESOLUTION_HZ,
        .invert_signal = false,
    };
    esp_err_t ret = ir_remote_init(&tx, NULL);
    if (ret != ESP_OK) return ret;
    g_ir_initialized = true;
    return ESP_OK;
}

esp_err_t bsp_ir_deinit(void)
{
    if (!g_ir_initialized) return ESP_OK;
    esp_err_t ret = ir_remote_deinit();
    g_ir_initialized = false;
    return ret;
}

bool bsp_ir_is_initialized(void)
{
    return g_ir_initialized;
}

esp_err_t bsp_ir_ac_quick_on(const char *brand, const char *model)
{
    if (!g_ir_initialized || !brand || !model) return ESP_ERR_INVALID_STATE;
    ir_device_info_t info = { .category = IR_DEVICE_AC };
    strncpy(info.brand, brand, IR_MAX_BRAND_NAME_LEN - 1);
    strncpy(info.model, model, IR_MAX_MODEL_NAME_LEN - 1);
    ir_ac_status_t st = { .power = IR_AC_POWER_ON, .mode = IR_AC_MODE_AUTO, .temperature = 24, .wind_speed = IR_AC_WIND_AUTO, .swing = IR_AC_SWING_OFF };
    return ir_send_ac_command(&info, &st);
}

esp_err_t bsp_ir_ac_quick_off(const char *brand, const char *model)
{
    if (!g_ir_initialized || !brand || !model) return ESP_ERR_INVALID_STATE;
    ir_device_info_t info = { .category = IR_DEVICE_AC };
    strncpy(info.brand, brand, IR_MAX_BRAND_NAME_LEN - 1);
    strncpy(info.model, model, IR_MAX_MODEL_NAME_LEN - 1);
    ir_ac_status_t st = { .power = IR_AC_POWER_OFF, .mode = IR_AC_MODE_AUTO, .temperature = 24, .wind_speed = IR_AC_WIND_AUTO, .swing = IR_AC_SWING_OFF };
    return ir_send_ac_command(&info, &st);
}

esp_err_t bsp_ir_ac_set_temp(const char *brand, const char *model, uint8_t temperature)
{
    if (!g_ir_initialized || !brand || !model) return ESP_ERR_INVALID_STATE;
    if (temperature < 16 || temperature > 30) return ESP_ERR_INVALID_ARG;
    ir_device_info_t info = { .category = IR_DEVICE_AC };
    strncpy(info.brand, brand, IR_MAX_BRAND_NAME_LEN - 1);
    strncpy(info.model, model, IR_MAX_MODEL_NAME_LEN - 1);
    ir_ac_status_t st = { .power = IR_AC_POWER_ON, .mode = IR_AC_MODE_COOL, .temperature = temperature, .wind_speed = IR_AC_WIND_AUTO, .swing = IR_AC_SWING_OFF };
    return ir_send_ac_command(&info, &st);
}

esp_err_t bsp_ir_tv_key(const char *brand, const char *model, ir_tv_keycode_t key)
{
    if (!g_ir_initialized || !brand || !model) return ESP_ERR_INVALID_STATE;
    ir_device_info_t info = { .category = IR_DEVICE_TV };
    strncpy(info.brand, brand, IR_MAX_BRAND_NAME_LEN - 1);
    strncpy(info.model, model, IR_MAX_MODEL_NAME_LEN - 1);
    return ir_send_tv_key(&info, key);
}

esp_err_t bsp_ir_tv_digit(const char *brand, const char *model, uint8_t digit)
{
    if (digit > 9) return ESP_ERR_INVALID_ARG;
    if (!g_ir_initialized || !brand || !model) return ESP_ERR_INVALID_STATE;
    ir_device_info_t info = { .category = IR_DEVICE_TV };
    strncpy(info.brand, brand, IR_MAX_BRAND_NAME_LEN - 1);
    strncpy(info.model, model, IR_MAX_MODEL_NAME_LEN - 1);
    return ir_send_tv_digit(&info, digit);
}

esp_err_t bsp_ir_send_timing_us(const uint32_t *timing_pairs_us, size_t length)
{
    if (!g_ir_initialized) return ESP_ERR_INVALID_STATE;
    return ir_send_timing_us(timing_pairs_us, length);
}



/* Vibration sensor functions */
esp_err_t bsp_vibration_sensor_init(void)
{
    ESP_LOGI(TAG, "Initializing vibration sensor on GPIO %d", BSP_VIBRATION_SENSOR_PIN);
    
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BSP_VIBRATION_SENSOR_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,  
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure vibration sensor GPIO: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Vibration sensor initialized successfully");
    return ESP_OK;
} 

bool bsp_vibration_sensor_read(void)
{
    vibration_detected = false;

    int current_level = gpio_get_level(BSP_VIBRATION_SENSOR_PIN);
    
    if (last_vibration_level != -1 && current_level != last_vibration_level) {
        vibration_level_change_count++;
        
        if (vibration_level_change_count >= g_vibration_sensitivity_threshold) {
            vibration_detected = true;
            vibration_level_change_count = 0;
        }
    }
    
    last_vibration_level = current_level;

    return vibration_detected;
}

esp_err_t bsp_vibration_sensor_set_sensitivity(int threshold)
{
    if (threshold < 1 || threshold > 50) {
        ESP_LOGE(TAG, "Invalid sensitivity threshold: %d (should be 1-50)", threshold);
        return ESP_ERR_INVALID_ARG;
    }
    
    g_vibration_sensitivity_threshold = threshold;
    
    vibration_level_change_count = 0;
    last_vibration_level = -1;
    vibration_detected = false;
    
    ESP_LOGI(TAG, "Vibration sensor sensitivity set to %d", threshold);
    return ESP_OK;
}



/* sleep & wakeup*/
bool bsp_woke_from_touch(void)
{
    return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO;
}

static esp_err_t bsp_sleep_enable_touch_wakeup(esp_deepsleep_gpio_wake_up_mode_t level, bool use_internal_pullup)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << BSP_TOUCH_INT),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = use_internal_pullup ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config failed for touch INT (GPIO %d): %s", BSP_TOUCH_INT, esp_err_to_name(err));
        return err;
    }

    err = esp_deep_sleep_enable_gpio_wakeup(1ULL << BSP_TOUCH_INT, level);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "enable gpio wakeup failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "GPIO wakeup enabled on GPIO %d, level=%s, int_pullup=%d",
             BSP_TOUCH_INT, (level == ESP_GPIO_WAKEUP_GPIO_LOW) ? "LOW" : "HIGH",
             use_internal_pullup);
    return ESP_OK;
}

static esp_err_t bsp_sleep_enter_now(void)
{
    ESP_LOGI(TAG, "Entering deep sleep");
    esp_deep_sleep_start();
    return ESP_OK; // not reached
}

static esp_err_t bsp_sleep_wait_touch_release_and_enter(uint32_t timeout_ms)
{
    // Determine active level by checking configured wake level via a simple heuristic:
    // If line is low most of the time and wake level is LOW, treat 0 as active.
    // Here we use current level assumption: active when equals current active state
    const int active_low = 1; // default assumption for BS8112A3 INT
    const int active_level = active_low ? 0 : 1;

    uint64_t start_ms = esp_timer_get_time() / 1000ULL;
    while (gpio_get_level(BSP_TOUCH_INT) == active_level) {
        if ((esp_timer_get_time() / 1000ULL - start_ms) > timeout_ms) break;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return bsp_sleep_enter_now();
}

esp_err_t bsp_sleep_quiesce(const bsp_sleep_prep_cfg_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;

    if (cfg->turn_off_backlight) {
        (void)bsp_display_backlight_off();
    }
    if (cfg->clear_leds) {
        (void)bsp_led_stop_effect();
        (void)bsp_led_clear_all();
    }
    if (cfg->stop_audio) {
        (void)audio_player_stop();
        // keep deinit decision to app; deinit increases wake latency
    }
    if (cfg->deinit_buttons) {
        (void)bsp_button_deinit();
    }
    if (cfg->deinit_i2c) {
        (void)bsp_i2c_deinit();
    }
    if (cfg->power_off_before_sleep) {
        // Caution: do NOT cut power to touch IC or RTC domain if touch wake is required
        bsp_power_control_set_power(false);
    }
    return ESP_OK;
}

esp_err_t bsp_sleep_prepare_and_enter(const bsp_sleep_prep_cfg_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;

    ESP_ERROR_CHECK(bsp_sleep_quiesce(cfg));
    ESP_ERROR_CHECK(bsp_sleep_enable_touch_wakeup(cfg->wake_level, cfg->use_internal_pullup));

    if (cfg->wait_touch_release) {
        return bsp_sleep_wait_touch_release_and_enter(cfg->wait_timeout_ms);
    }
    return bsp_sleep_enter_now();
}

esp_err_t bsp_touch_wakeup_clear_irq(void)
{
    // Ensure I2C bus exists
    if (bsp_i2c_get_bus_handle() == NULL) {
        bsp_i2c_config_t i2c_cfg = {
            .sda_io_num = BSP_I2C_SDA,
            .scl_io_num = BSP_I2C_SCL,
            .clk_speed  = BSP_I2C_CLK_SPEED,
            .enable_internal_pullup = true,
        };
        esp_err_t e = bsp_i2c_init(&i2c_cfg);
        if (e != ESP_OK) {
            ESP_LOGE(TAG, "I2C init failed in wake IRQ clear: %s", esp_err_to_name(e));
            return e;
        }
    }

    // Minimal touch IC init without ISR to just read status once
    if (!touch_ic_bs8112a3_is_initialized()) {
        touch_ic_bs8112a3_config_t touch_cfg = {
            .i2c_bus_handle = bsp_i2c_get_bus_handle(),
            .device_address = BS8112A3_I2C_ADDR,
            .scl_speed_hz   = BSP_I2C_CLK_SPEED,
            .interrupt_pin  = GPIO_NUM_NC, // no ISR for early clear
        };
        esp_err_t e = touch_ic_bs8112a3_init(&touch_cfg);
        if (e != ESP_OK) {
            ESP_LOGE(TAG, "Touch IC early init failed: %s", esp_err_to_name(e));
            return e;
        }
    }

    uint8_t status = touch_ic_bs8112a3_get_key_status();
    ESP_LOGI(TAG, "Touch status read after wake: 0x%02X (IRQ cleared)", status);

    // Deinit to avoid duplication with later full init path (optional)
    esp_err_t de = touch_ic_bs8112a3_deinit();
    if (de != ESP_OK) {
        ESP_LOGW(TAG, "Touch IC deinit after wake returned: %s", esp_err_to_name(de));
        // keep going
    }
    return ESP_OK;
}

