/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_err.h"
#include "ir_remote_control.h"
#include "irext_api.h"
#include "irext_auth.h"
#include "ir_transmitter.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "ir_remote_control";

// Global state
static bool g_ir_initialized = false;
static ir_tx_config_t g_tx_config = {0};
static ir_irext_config_t g_irext_config = {0};

static esp_err_t resolve_model_if_needed(ir_device_info_t *info)
{
    if (info->model_id != 0) return ESP_OK;
    uint32_t bid = 0, mid = 0;
    esp_err_t e = irext_api_find_device(info->category, info->brand, info->model, &bid, &mid);
    if (e != ESP_OK) return e;
    info->brand_id = bid;
    info->model_id = mid;
    return ESP_OK;
}

esp_err_t ir_remote_init(const ir_tx_config_t *tx_config, const ir_irext_config_t *irext_config)
{
    if (g_ir_initialized) {
        ESP_LOGW(TAG, "IR system already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!tx_config) {
        ESP_LOGE(TAG, "TX config is required");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "=== Initializing IRext Online IR Remote Control ===");
    ESP_LOGI(TAG, "  TX GPIO: %d, Carrier: %dHz", tx_config->tx_gpio, tx_config->carrier_freq_hz);
    ESP_LOGI(TAG, "  Resolution: %dHz", tx_config->resolution_hz);
    ESP_LOGI(TAG, "  Invert Signal: %s", tx_config->invert_signal ? "Yes" : "No");

    // Initialize IR transmitter hardware
    esp_err_t ret = ir_transmitter_init(tx_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize IR transmitter: %s", esp_err_to_name(ret));
        return ret;
    }

    // Store configurations
    memcpy(&g_tx_config, tx_config, sizeof(ir_tx_config_t));
    if (irext_config) {
        ESP_LOGI(TAG, "Using provided IRext config");
        memcpy(&g_irext_config, irext_config, sizeof(ir_irext_config_t));
    } else {
        ESP_LOGI(TAG, "Using Kconfig defaults for IRext");
        // Use Kconfig defaults
        g_irext_config.server_url = CONFIG_IR_SERVER_URL;
        g_irext_config.app_key = CONFIG_IR_APP_KEY;
        g_irext_config.app_secret = CONFIG_IR_APP_SECRET;
        g_irext_config.auto_login = CONFIG_IR_AUTO_LOGIN;
#ifdef CONFIG_IR_TOKEN_CACHE_ENABLED
        g_irext_config.cache_token = CONFIG_IR_TOKEN_CACHE_ENABLED;
#else
        g_irext_config.cache_token = false;
#endif
        g_irext_config.timeout_ms = CONFIG_IR_API_TIMEOUT_MS;
    }
    
    // Log the configuration details
    ESP_LOGI(TAG, "IRext Configuration:");
    ESP_LOGI(TAG, "  Server URL: %s", g_irext_config.server_url);
    ESP_LOGI(TAG, "  Auto Login: %s", g_irext_config.auto_login ? "Enabled" : "Disabled");
    ESP_LOGI(TAG, "  Token Cache: %s", g_irext_config.cache_token ? "Enabled" : "Disabled");
    ESP_LOGI(TAG, "  Timeout: %dms", g_irext_config.timeout_ms);
    ESP_LOGI(TAG, "  APP Key: %.10s... (length: %d)", g_irext_config.app_key, strlen(g_irext_config.app_key));
    ESP_LOGI(TAG, "  APP Secret: %.10s... (length: %d)", g_irext_config.app_secret, strlen(g_irext_config.app_secret));

    // Initialize IRext authentication
    ret = irext_auth_init(&g_irext_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize IRext authentication: %s", esp_err_to_name(ret));
        ir_transmitter_deinit();
        return ret;
    }

    // Initialize IRext API client
    ret = irext_api_init(&g_irext_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize IRext API: %s", esp_err_to_name(ret));
        irext_auth_deinit();
        ir_transmitter_deinit();
        return ret;
    }

    g_ir_initialized = true;
    
    // 检查认证状态
    ESP_LOGI(TAG, "Checking authentication status...");
    bool auth_valid = irext_auth_is_valid();
    ESP_LOGI(TAG, "Authentication status: %s", auth_valid ? "VALID" : "INVALID");
    
    if (!auth_valid && g_irext_config.auto_login) {
        ESP_LOGW(TAG, "Auto login enabled but auth invalid - login should have been attempted");
    }
    
    ESP_LOGI(TAG, "=== IRext Online IR Remote Control initialized successfully ===");
    ESP_LOGI(TAG, "Server: %s", g_irext_config.server_url);

    return ESP_OK;
}

esp_err_t ir_remote_deinit(void)
{
    if (!g_ir_initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Deinitializing IR remote control system");

    // Cleanup in reverse order
    irext_api_deinit();
    irext_auth_deinit();
    ir_transmitter_deinit();

    g_ir_initialized = false;
    return ESP_OK;
}

esp_err_t ir_send_ac_command(const ir_device_info_t *device_info, const ir_ac_status_t *ac_status)
{
    if (!g_ir_initialized) {
        ESP_LOGE(TAG, "IR system not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!device_info || !ac_status) {
        ESP_LOGE(TAG, "Invalid parameters: device_info=%p, ac_status=%p", device_info, ac_status);
        return ESP_ERR_INVALID_ARG;
    }

    if (!device_info->brand[0] || !device_info->model[0]) {
        ESP_LOGE(TAG, "Empty brand or model name");
        return ESP_ERR_INVALID_ARG;
    }

    if (ac_status->temperature < 16 || ac_status->temperature > 30) {
        ESP_LOGE(TAG, "Temperature %d out of range (16-30°C)", ac_status->temperature);
        return ESP_ERR_INVALID_ARG;
    }

    ir_device_info_t resolved = *device_info;
    if (resolved.model_id == 0) {
        uint32_t bid = 0, mid = 0;
        esp_err_t e = irext_api_find_device(resolved.category, resolved.brand, resolved.model, &bid, &mid);
        if (e != ESP_OK) {
            return e;
        }
        resolved.brand_id = bid;
        resolved.model_id = mid;
    }

    ESP_LOGI(TAG, "Sending AC command: %s %s, power=%d, temp=%d°C",
             resolved.brand, resolved.model,
             ac_status->power, ac_status->temperature);

    return irext_api_send_ac_command(&resolved, ac_status);
}

esp_err_t ir_send_tv_key(const ir_device_info_t *device_info, ir_tv_keycode_t key)
{
    if (!g_ir_initialized) return ESP_ERR_INVALID_STATE;
    if (!device_info) return ESP_ERR_INVALID_ARG;

    ir_device_info_t resolved = *device_info;
    resolved.category = IR_DEVICE_TV; // enforce category
    if (resolved.model_id == 0) {
        uint32_t bid = 0, mid = 0;
        esp_err_t e = irext_api_find_device(IR_DEVICE_TV, resolved.brand, resolved.model, &bid, &mid);
        if (e != ESP_OK) return e;
        resolved.brand_id = bid;
        resolved.model_id = mid;
    }
    return irext_api_send_key_command(&resolved, (uint32_t)key);
}

esp_err_t ir_send_stb_key(const ir_device_info_t *device_info, ir_stb_keycode_t key)
{
    if (!g_ir_initialized) return ESP_ERR_INVALID_STATE;
    if (!device_info) return ESP_ERR_INVALID_ARG;

    ir_device_info_t resolved = *device_info;
    resolved.category = IR_DEVICE_STB; // enforce category
    if (resolved.model_id == 0) {
        uint32_t bid = 0, mid = 0;
        esp_err_t e = irext_api_find_device(IR_DEVICE_STB, resolved.brand, resolved.model, &bid, &mid);
        if (e != ESP_OK) return e;
        resolved.brand_id = bid;
        resolved.model_id = mid;
    }
    return irext_api_send_key_command(&resolved, (uint32_t)key);
}

esp_err_t ir_send_tv_digit(const ir_device_info_t *device_info, uint8_t digit)
{
    if (digit > 9) return ESP_ERR_INVALID_ARG;
    return ir_send_tv_key(device_info, (ir_tv_keycode_t)IR_TV_KEY_DIGIT(digit));
}

esp_err_t ir_send_stb_digit(const ir_device_info_t *device_info, uint8_t digit)
{
    if (digit > 9) return ESP_ERR_INVALID_ARG;
    return ir_send_stb_key(device_info, (ir_stb_keycode_t)IR_STB_KEY_DIGIT(digit));
}

esp_err_t ir_send_key_command(const ir_device_info_t *device_info, uint32_t key_code)
{
    if (!g_ir_initialized) {
        ESP_LOGE(TAG, "IR system not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!device_info) {
        return ESP_ERR_INVALID_ARG;
    }

    ir_device_info_t resolved = *device_info;
    if (resolved.model_id == 0) {
        uint32_t bid = 0, mid = 0;
        esp_err_t e = irext_api_find_device(resolved.category, resolved.brand, resolved.model, &bid, &mid);
        if (e != ESP_OK) {
            return e;
        }
        resolved.brand_id = bid;
        resolved.model_id = mid;
    }

    ESP_LOGI(TAG, "Sending key command: %s %s, key=0x%02x",
             resolved.brand, resolved.model, key_code);

    return irext_api_send_key_command(&resolved, key_code);
}

esp_err_t ir_send_ac_key_command(const ir_device_info_t *device_info,
                                 ir_ac_keycode_t key,
                                 const ir_ac_status_t *ac_status,
                                 bool change_wind_dir)
{
    if (!g_ir_initialized) return ESP_ERR_INVALID_STATE;
    if (!device_info || !ac_status) return ESP_ERR_INVALID_ARG;
    if (ac_status->temperature < 16 || ac_status->temperature > 30) return ESP_ERR_INVALID_ARG;

    ir_device_info_t resolved = *device_info;
    if (resolved.model_id == 0) {
        uint32_t bid = 0, mid = 0;
        esp_err_t e = irext_api_find_device(IR_DEVICE_AC, resolved.brand, resolved.model, &bid, &mid);
        if (e != ESP_OK) return e;
        resolved.brand_id = bid;
        resolved.model_id = mid;
    }

    uint32_t *ir_data = NULL;
    size_t length = 0;
    esp_err_t err = irext_api_decode_command(resolved.model_id, (uint32_t)key, ac_status,
                                             change_wind_dir ? 1 : 0, 0, &ir_data, &length);
    if (err != ESP_OK) return err;
    err = ir_transmitter_send_timing_data(ir_data, length);
    free(ir_data);
    return err;
}

esp_err_t ir_get_supported_brands(ir_device_category_t category, 
                                 char brands[][IR_MAX_BRAND_NAME_LEN],
                                 size_t max_brands, 
                                 size_t *found_count)
{
    if (!g_ir_initialized) {
        ESP_LOGE(TAG, "IR system not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    return irext_api_get_brands(category, brands, max_brands, found_count);
}

esp_err_t ir_get_supported_models(ir_device_category_t category,
                                 const char *brand,
                                 char models[][IR_MAX_MODEL_NAME_LEN],
                                 size_t max_models, 
                                 size_t *found_count)
{
    if (!g_ir_initialized) {
        ESP_LOGE(TAG, "IR system not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    return irext_api_get_models(category, brand, models, max_models, found_count);
}

esp_err_t ir_search_brands(ir_device_category_t category,
                          const char *pattern,
                          char brands[][IR_MAX_BRAND_NAME_LEN],
                          size_t max_brands, 
                          size_t *found_count)
{
    if (!g_ir_initialized) {
        ESP_LOGE(TAG, "IR system not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    return irext_api_search_brands(category, pattern, brands, max_brands, found_count);
}

esp_err_t ir_search_models(ir_device_category_t category,
                          const char *brand,
                          const char *pattern,
                          char models[][IR_MAX_MODEL_NAME_LEN],
                          size_t max_models, 
                          size_t *found_count)
{
    if (!g_ir_initialized) {
        ESP_LOGE(TAG, "IR system not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    return irext_api_search_models(category, brand, pattern, models, max_models, found_count);
}

esp_err_t ir_find_device_by_name(ir_device_category_t category,
                                 const char *brand,
                                 const char *model,
                                 uint32_t *brand_id,
                                 uint32_t *model_id)
{
    if (!g_ir_initialized) {
        ESP_LOGE(TAG, "IR system not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    return irext_api_find_device(category, brand, model, brand_id, model_id);
}

esp_err_t ir_send_timing_us(const uint32_t *timing_pairs_us, size_t length)
{
    if (!g_ir_initialized) {
        ESP_LOGE(TAG, "IR system not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    if (!timing_pairs_us || length == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return ir_transmitter_send_timing_data(timing_pairs_us, length);
}

bool ir_is_initialized(void)
{
    return g_ir_initialized;
}

esp_err_t ir_refresh_auth_token(void)
{
    if (!g_ir_initialized) {
        ESP_LOGE(TAG, "IR system not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    return irext_auth_refresh_if_needed();
}

esp_err_t ir_clear_cache(void)
{
    if (!g_ir_initialized) {
        ESP_LOGE(TAG, "IR system not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret1 = irext_auth_clear_cache();
    esp_err_t ret2 = irext_api_clear_cache();
    
    return (ret1 != ESP_OK) ? ret1 : ret2;
}

esp_err_t ir_get_last_error(void)
{
    return irext_api_get_last_error();
}

esp_err_t ir_get_categories(ir_category_t *categories, size_t max_categories, size_t *found_count)
{
    if (!g_ir_initialized) {
        ESP_LOGE(TAG, "IR system not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    return irext_api_get_categories(categories, max_categories, found_count);
}

esp_err_t ir_get_ac_parameters(uint32_t index_id, uint8_t mode, ir_ac_parameters_t *parameters)
{
    if (!g_ir_initialized) {
        ESP_LOGE(TAG, "IR system not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    return irext_api_get_ac_parameters(index_id, mode, parameters);
}

esp_err_t ir_get_brand_id(ir_device_category_t category, const char *brand, uint32_t *brand_id)
{
    if (!g_ir_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    return irext_api_get_brand_id(category, brand, brand_id);
}

esp_err_t ir_remote_init_with_defaults(uint8_t tx_gpio)
{
    ir_tx_config_t tx_config = {
        .tx_gpio = tx_gpio,
        .carrier_freq_hz = 38000,
        .resolution_hz = 1000000,
        .invert_signal = false
    };
    
    return ir_remote_init(&tx_config, NULL);
}

esp_err_t ir_send_box_key(const ir_device_info_t *device_info, ir_box_keycode_t key)
{
    if (!g_ir_initialized || !device_info) return ESP_ERR_INVALID_STATE;
    ir_device_info_t resolved = *device_info;
    resolved.category = IR_DEVICE_BOX; // enforce category
    esp_err_t e = resolve_model_if_needed(&resolved);
    if (e != ESP_OK) return e;
    return irext_api_send_key_command(&resolved, (uint32_t)key);
}

esp_err_t ir_send_iptv_key(const ir_device_info_t *device_info, ir_iptv_keycode_t key)
{
    if (!g_ir_initialized || !device_info) return ESP_ERR_INVALID_STATE;
    ir_device_info_t resolved = *device_info;
    resolved.category = IR_DEVICE_IPTV; // enforce category
    esp_err_t e = resolve_model_if_needed(&resolved);
    if (e != ESP_OK) return e;
    return irext_api_send_key_command(&resolved, (uint32_t)key);
}

esp_err_t ir_send_dvd_key(const ir_device_info_t *device_info, ir_dvd_keycode_t key)
{
    if (!g_ir_initialized || !device_info) return ESP_ERR_INVALID_STATE;
    ir_device_info_t resolved = *device_info;
    resolved.category = IR_DEVICE_DVD; // enforce category
    esp_err_t e = resolve_model_if_needed(&resolved);
    if (e != ESP_OK) return e;
    return irext_api_send_key_command(&resolved, (uint32_t)key);
}

esp_err_t ir_send_fan_key(const ir_device_info_t *device_info, ir_fan_keycode_t key)
{
    if (!g_ir_initialized || !device_info) return ESP_ERR_INVALID_STATE;
    ir_device_info_t resolved = *device_info;
    resolved.category = IR_DEVICE_FAN; // enforce category
    esp_err_t e = resolve_model_if_needed(&resolved);
    if (e != ESP_OK) return e;
    return irext_api_send_key_command(&resolved, (uint32_t)key);
}

esp_err_t ir_send_projector_key(const ir_device_info_t *device_info, ir_projector_keycode_t key)
{
    if (!g_ir_initialized || !device_info) return ESP_ERR_INVALID_STATE;
    ir_device_info_t resolved = *device_info;
    resolved.category = IR_DEVICE_PROJECTOR; // enforce category
    esp_err_t e = resolve_model_if_needed(&resolved);
    if (e != ESP_OK) return e;
    return irext_api_send_key_command(&resolved, (uint32_t)key);
}

esp_err_t ir_send_stereo_key(const ir_device_info_t *device_info, ir_stereo_keycode_t key)
{
    if (!g_ir_initialized || !device_info) return ESP_ERR_INVALID_STATE;
    ir_device_info_t resolved = *device_info;
    resolved.category = IR_DEVICE_STEREO; // enforce category
    esp_err_t e = resolve_model_if_needed(&resolved);
    if (e != ESP_OK) return e;
    return irext_api_send_key_command(&resolved, (uint32_t)key);
}

esp_err_t ir_send_bulb_key(const ir_device_info_t *device_info, ir_bulb_keycode_t key)
{
    if (!g_ir_initialized || !device_info) return ESP_ERR_INVALID_STATE;
    ir_device_info_t resolved = *device_info;
    resolved.category = IR_DEVICE_BULB; // enforce category
    esp_err_t e = resolve_model_if_needed(&resolved);
    if (e != ESP_OK) return e;
    return irext_api_send_key_command(&resolved, (uint32_t)key);
}

esp_err_t ir_send_robot_key(const ir_device_info_t *device_info, ir_robot_keycode_t key)
{
    if (!g_ir_initialized || !device_info) return ESP_ERR_INVALID_STATE;
    ir_device_info_t resolved = *device_info;
    resolved.category = IR_DEVICE_ROBOT_VACUUM; // enforce category
    esp_err_t e = resolve_model_if_needed(&resolved);
    if (e != ESP_OK) return e;
    return irext_api_send_key_command(&resolved, (uint32_t)key);
}

esp_err_t ir_send_air_cleaner_key(const ir_device_info_t *device_info, ir_air_cleaner_keycode_t key)
{
    if (!g_ir_initialized || !device_info) return ESP_ERR_INVALID_STATE;
    ir_device_info_t resolved = *device_info;
    resolved.category = IR_DEVICE_AIR_PURIFIER; // enforce category
    esp_err_t e = resolve_model_if_needed(&resolved);
    if (e != ESP_OK) return e;
    return irext_api_send_key_command(&resolved, (uint32_t)key);
}

esp_err_t ir_send_dyson_key(const ir_device_info_t *device_info, ir_dyson_keycode_t key)
{
    if (!g_ir_initialized || !device_info) return ESP_ERR_INVALID_STATE;
    ir_device_info_t resolved = *device_info;
    resolved.category = IR_DEVICE_DYSON; // enforce category
    esp_err_t e = resolve_model_if_needed(&resolved);
    if (e != ESP_OK) return e;
    return irext_api_send_key_command(&resolved, (uint32_t)key);
}

esp_err_t ir_send_camera_key(const ir_device_info_t *device_info, ir_camera_keycode_t key)
{
    if (!g_ir_initialized || !device_info) return ESP_ERR_INVALID_STATE;
    ir_device_info_t resolved = *device_info;
    resolved.category = IR_DEVICE_CAMERA; // enforce category
    esp_err_t e = resolve_model_if_needed(&resolved);
    if (e != ESP_OK) return e;
    return irext_api_send_key_command(&resolved, (uint32_t)key);
}

esp_err_t ir_send_heater_key(const ir_device_info_t *device_info, ir_heater_keycode_t key)
{
    if (!g_ir_initialized || !device_info) return ESP_ERR_INVALID_STATE;
    ir_device_info_t resolved = *device_info;
    resolved.category = IR_DEVICE_HEATER; // enforce category
    esp_err_t e = resolve_model_if_needed(&resolved);
    if (e != ESP_OK) return e;
    return irext_api_send_key_command(&resolved, (uint32_t)key);
}