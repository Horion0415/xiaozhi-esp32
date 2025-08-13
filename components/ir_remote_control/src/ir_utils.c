/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "esp_log.h"
#include "esp_err.h"
#include "ir_remote_control.h"

static const char *TAG = "ir_utils";

esp_err_t ir_utils_validate_config(const ir_irext_config_t *config)
{
    if (!config) {
        ESP_LOGE(TAG, "Config is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (!config->server_url || strlen(config->server_url) == 0) {
        ESP_LOGE(TAG, "Server URL is empty");
        return ESP_ERR_INVALID_ARG;
    }

    if (!config->app_key || strlen(config->app_key) == 0) {
        ESP_LOGE(TAG, "APP Key is empty");
        return ESP_ERR_INVALID_ARG;
    }

    if (!config->app_secret || strlen(config->app_secret) == 0) {
        ESP_LOGE(TAG, "APP Secret is empty");
        return ESP_ERR_INVALID_ARG;
    }

    if (config->timeout_ms < 1000 || config->timeout_ms > 60000) {
        ESP_LOGW(TAG, "Timeout %d ms is out of recommended range (1000-60000)", config->timeout_ms);
    }

    return ESP_OK;
}

esp_err_t ir_utils_validate_ac_status(const ir_ac_status_t *status)
{
    if (!status) {
        ESP_LOGE(TAG, "AC status is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (status->power > IR_AC_POWER_ON) {
        ESP_LOGE(TAG, "Invalid AC power state: %d", status->power);
        return ESP_ERR_INVALID_ARG;
    }

    if (status->mode > IR_AC_MODE_FAN) {
        ESP_LOGE(TAG, "Invalid AC mode: %d", status->mode);
        return ESP_ERR_INVALID_ARG;
    }

    if (status->temperature < 16 || status->temperature > 30) {
        ESP_LOGE(TAG, "Temperature %d out of range (16-30)", status->temperature);
        return ESP_ERR_INVALID_ARG;
    }

    if (status->wind_speed > IR_AC_WIND_HIGH) {
        ESP_LOGE(TAG, "Invalid wind speed: %d", status->wind_speed);
        return ESP_ERR_INVALID_ARG;
    }

    if (status->swing > IR_AC_SWING_ON) {
        ESP_LOGE(TAG, "Invalid swing setting: %d", status->swing);
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

esp_err_t ir_utils_validate_device_info(const ir_device_info_t *device_info)
{
    if (!device_info) {
        ESP_LOGE(TAG, "Device info is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (device_info->category < IR_DEVICE_AC || device_info->category >= IR_DEVICE_MAX) {
        ESP_LOGE(TAG, "Invalid device category: %d", device_info->category);
        return ESP_ERR_INVALID_ARG;
    }

    if (strlen(device_info->brand) == 0) {
        ESP_LOGE(TAG, "Brand name is empty");
        return ESP_ERR_INVALID_ARG;
    }

    if (strlen(device_info->model) == 0) {
        ESP_LOGE(TAG, "Model name is empty");
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

void ir_utils_normalize_string(char *str)
{
    if (!str) return;

    char *src = str;
    char *dst = str;

    while (*src) {
        if (isalnum((unsigned char)*src) || *src == '-' || *src == '_') {
            *dst++ = tolower((unsigned char)*src);
        } else if (*src == ' ' && dst > str && *(dst - 1) != '_') {
            *dst++ = '_';
        }
        src++;
    }

    if (dst > str && *(dst - 1) == '_') {
        dst--;
    }

    *dst = '\0';
}

esp_err_t ir_utils_hex_to_bytes(const char *hex_str, uint8_t *bytes, size_t max_bytes, size_t *actual_bytes)
{
    if (!hex_str || !bytes || !actual_bytes) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t hex_len = strlen(hex_str);
    if (hex_len % 2 != 0) {
        ESP_LOGE(TAG, "Hex string length must be even");
        return ESP_ERR_INVALID_ARG;
    }

    *actual_bytes = hex_len / 2;
    if (*actual_bytes > max_bytes) {
        ESP_LOGE(TAG, "Byte array too small: need %d, have %d", *actual_bytes, max_bytes);
        return ESP_ERR_NO_MEM;
    }

    for (size_t i = 0; i < *actual_bytes; i++) {
        unsigned int byte;
        if (sscanf(hex_str + i * 2, "%2x", &byte) != 1) {
            ESP_LOGE(TAG, "Invalid hex character at position %d", i * 2);
            return ESP_ERR_INVALID_ARG;
        }
        bytes[i] = (uint8_t)byte;
    }

    return ESP_OK;
}

esp_err_t ir_utils_bytes_to_hex(const uint8_t *bytes, size_t byte_count, char *hex_str, size_t hex_str_size)
{
    if (!bytes || !hex_str || byte_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (hex_str_size < byte_count * 2 + 1) {
        ESP_LOGE(TAG, "Hex string buffer too small: need %d, have %d", byte_count * 2 + 1, hex_str_size);
        return ESP_ERR_NO_MEM;
    }

    for (size_t i = 0; i < byte_count; i++) {
        sprintf(hex_str + i * 2, "%02x", bytes[i]);
    }

    hex_str[byte_count * 2] = '\0';
    return ESP_OK;
}

const char *ir_utils_get_category_name(ir_device_category_t category)
{
    switch (category) {
        case IR_DEVICE_AC: return "Air Conditioner";
        case IR_DEVICE_TV: return "Television";
        case IR_DEVICE_STB: return "Set-Top Box";
        case IR_DEVICE_DVD: return "DVD Player";
        case IR_DEVICE_FAN: return "Electric Fan";
        case IR_DEVICE_LIGHT: return "Smart Light";
        case IR_DEVICE_PROJECTOR: return "Projector";
        case IR_DEVICE_STEREO: return "Stereo System";
        case IR_DEVICE_AIR_PURIFIER: return "Air Purifier";
        case IR_DEVICE_ROBOT_VACUUM: return "Robot Vacuum";
        default: return "Unknown";
    }
}

const char *ir_utils_get_ac_mode_name(ir_ac_mode_t mode)
{
    switch (mode) {
        case IR_AC_MODE_AUTO: return "Auto";
        case IR_AC_MODE_COOL: return "Cool";
        case IR_AC_MODE_HEAT: return "Heat";
        case IR_AC_MODE_DRY: return "Dry";
        case IR_AC_MODE_FAN: return "Fan";
        default: return "Unknown";
    }
}

const char *ir_utils_get_ac_wind_speed_name(ir_ac_wind_speed_t speed)
{
    switch (speed) {
        case IR_AC_WIND_AUTO: return "Auto";
        case IR_AC_WIND_LOW: return "Low";
        case IR_AC_WIND_MEDIUM: return "Medium";
        case IR_AC_WIND_HIGH: return "High";
        default: return "Unknown";
    }
}

esp_err_t ir_utils_create_device_key(const ir_device_info_t *device_info, char *key, size_t key_size)
{
    if (!device_info || !key) {
        return ESP_ERR_INVALID_ARG;
    }

    int ret = snprintf(key, key_size, "%d_%s_%s", 
                       device_info->category, 
                       device_info->brand, 
                       device_info->model);

    if (ret >= key_size) {
        ESP_LOGE(TAG, "Key buffer too small");
        return ESP_ERR_NO_MEM;
    }

    ir_utils_normalize_string(key);
    return ESP_OK;
}

bool ir_utils_is_valid_url(const char *url)
{
    if (!url || strlen(url) < 8) {
        return false;
    }

    return (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0);
}

uint32_t ir_utils_calculate_checksum(const uint8_t *data, size_t length)
{
    if (!data || length == 0) {
        return 0;
    }

    uint32_t checksum = 0;
    for (size_t i = 0; i < length; i++) {
        checksum += data[i];
    }

    return checksum;
} 