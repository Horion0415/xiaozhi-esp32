/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "ir_keymap.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Public constants */
#define IR_MAX_BRAND_NAME_LEN    32
#define IR_MAX_MODEL_NAME_LEN    32

/* Device categories */
typedef enum {
    IR_DEVICE_AC = 1,
    IR_DEVICE_TV = 2,
    IR_DEVICE_STB = 3,
    IR_DEVICE_DVD = 4,
    IR_DEVICE_FAN = 5,
    IR_DEVICE_LIGHT = 6,
    IR_DEVICE_PROJECTOR = 7,
    IR_DEVICE_STEREO = 8,
    IR_DEVICE_AIR_PURIFIER = 9,
    IR_DEVICE_ROBOT_VACUUM = 10,
    /* Extended categories for completeness */
    IR_DEVICE_BOX = 11,
    IR_DEVICE_IPTV = 12,
    IR_DEVICE_BULB = 13,
    IR_DEVICE_DYSON = 14,
    IR_DEVICE_CAMERA = 15,
    IR_DEVICE_HEATER = 16,
} ir_device_category_t;

/* AC states */
typedef enum { IR_AC_POWER_OFF = 0, IR_AC_POWER_ON = 1 } ir_ac_power_t;

typedef enum {
    IR_AC_MODE_AUTO = 0,
    IR_AC_MODE_COOL = 1,
    IR_AC_MODE_HEAT = 2,
    IR_AC_MODE_DRY = 3,
    IR_AC_MODE_FAN = 4
} ir_ac_mode_t;

typedef enum {
    IR_AC_WIND_AUTO = 0,
    IR_AC_WIND_LOW = 1,
    IR_AC_WIND_MEDIUM = 2,
    IR_AC_WIND_HIGH = 3
} ir_ac_wind_speed_t;

typedef enum {
    IR_AC_SWING_OFF = 0,
    IR_AC_SWING_ON = 1
} ir_ac_swing_t;

/* AC status */
typedef struct {
    ir_ac_power_t power;
    ir_ac_mode_t mode;
    uint8_t temperature;          /* 16-30°C */
    ir_ac_wind_speed_t wind_speed;
    ir_ac_swing_t swing;
} ir_ac_status_t;

/* Device info */
typedef struct {
    ir_device_category_t category;
    char brand[IR_MAX_BRAND_NAME_LEN];
    char model[IR_MAX_MODEL_NAME_LEN];
    uint32_t brand_id;
    uint32_t model_id;
} ir_device_info_t;

/* TX config */
typedef struct {
    uint8_t tx_gpio;
    uint32_t carrier_freq_hz;   /* 38000 or 56000 */
    uint32_t resolution_hz;     /* RMT resolution */
    bool invert_signal;
} ir_tx_config_t;

/* IRext auth config */
typedef struct {
    const char *server_url;
    const char *app_key;
    const char *app_secret;
    bool auto_login;
    bool cache_token;
    uint32_t timeout_ms;
} ir_irext_config_t;

/* Categories from Web API */
typedef struct {
    uint32_t id;
    char name[IR_MAX_BRAND_NAME_LEN];
    uint32_t status;
} ir_category_t;

/* AC capability */
typedef struct {
    uint8_t temp_min;
    uint8_t temp_max;
    uint8_t supported_modes[5];
    uint8_t supported_wind_speed[4];
    uint8_t supported_swing[2];
    uint8_t supported_wind_directions;
} ir_ac_parameters_t;

/* Init/Deinit */
esp_err_t ir_remote_init(const ir_tx_config_t *tx_config, const ir_irext_config_t *irext_config);
esp_err_t ir_remote_deinit(void);

/* Send commands */
esp_err_t ir_send_ac_key_command(const ir_device_info_t *device_info,
                                 ir_ac_keycode_t key,
                                 const ir_ac_status_t *ac_status,
                                 bool change_wind_dir);

esp_err_t ir_send_tv_key(const ir_device_info_t *device_info, ir_tv_keycode_t key);
esp_err_t ir_send_stb_key(const ir_device_info_t *device_info, ir_stb_keycode_t key);
esp_err_t ir_send_box_key(const ir_device_info_t *device_info, ir_box_keycode_t key);
esp_err_t ir_send_iptv_key(const ir_device_info_t *device_info, ir_iptv_keycode_t key);
esp_err_t ir_send_dvd_key(const ir_device_info_t *device_info, ir_dvd_keycode_t key);
esp_err_t ir_send_fan_key(const ir_device_info_t *device_info, ir_fan_keycode_t key);
esp_err_t ir_send_projector_key(const ir_device_info_t *device_info, ir_projector_keycode_t key);
esp_err_t ir_send_stereo_key(const ir_device_info_t *device_info, ir_stereo_keycode_t key);
esp_err_t ir_send_bulb_key(const ir_device_info_t *device_info, ir_bulb_keycode_t key);
esp_err_t ir_send_robot_key(const ir_device_info_t *device_info, ir_robot_keycode_t key);
esp_err_t ir_send_air_cleaner_key(const ir_device_info_t *device_info, ir_air_cleaner_keycode_t key);
esp_err_t ir_send_dyson_key(const ir_device_info_t *device_info, ir_dyson_keycode_t key);
esp_err_t ir_send_camera_key(const ir_device_info_t *device_info, ir_camera_keycode_t key);
esp_err_t ir_send_heater_key(const ir_device_info_t *device_info, ir_heater_keycode_t key);

/* Generic key and digits */
esp_err_t ir_send_key_command(const ir_device_info_t *device_info, uint32_t key_code);
esp_err_t ir_send_tv_digit(const ir_device_info_t *device_info, uint8_t digit);
esp_err_t ir_send_stb_digit(const ir_device_info_t *device_info, uint8_t digit);

/* Compatibility (prefer ir_send_ac_key_command) */
esp_err_t ir_send_ac_command(const ir_device_info_t *device_info, const ir_ac_status_t *ac_status);

/* Listing & search */
esp_err_t ir_get_categories(ir_category_t *categories, size_t max_categories, size_t *found_count);
esp_err_t ir_get_supported_brands(ir_device_category_t category, char brands[][IR_MAX_BRAND_NAME_LEN], size_t max_brands, size_t *found_count);
esp_err_t ir_get_supported_models(ir_device_category_t category, const char *brand, char models[][IR_MAX_MODEL_NAME_LEN], size_t max_models, size_t *found_count);
esp_err_t ir_search_brands(ir_device_category_t category, const char *pattern, char brands[][IR_MAX_BRAND_NAME_LEN], size_t max_brands, size_t *found_count);
esp_err_t ir_search_models(ir_device_category_t category, const char *brand, const char *pattern, char models[][IR_MAX_MODEL_NAME_LEN], size_t max_models, size_t *found_count);
esp_err_t ir_find_device_by_name(ir_device_category_t category, const char *brand, const char *model, uint32_t *brand_id, uint32_t *model_id);
esp_err_t ir_get_brand_id(ir_device_category_t category, const char *brand, uint32_t *brand_id);

/* AC capability */
esp_err_t ir_get_ac_parameters(uint32_t index_id, uint8_t mode, ir_ac_parameters_t *parameters);

/* Low-level */
esp_err_t ir_send_timing_us(const uint32_t *timing_pairs_us, size_t length);

/* Status */
bool ir_is_initialized(void);
esp_err_t ir_refresh_auth_token(void);
esp_err_t ir_clear_cache(void);
esp_err_t ir_get_last_error(void);

/* Minimal BSP helper */
esp_err_t ir_remote_init_with_defaults(uint8_t tx_gpio);

#ifdef __cplusplus
}
#endif 