/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_err.h"
#include "cJSON.h"
#include "esp_heap_caps.h"

static const char *TAG = "ir_json_parser";

esp_err_t ir_json_parse_string(const char *json_str, const char *key, char *output, size_t output_size)
{
    if (!json_str || !key || !output) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *json = cJSON_Parse(json_str);
    if (!json) {
        ESP_LOGE(TAG, "Failed to parse JSON");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *item = cJSON_GetObjectItem(json, key);
    if (!item || !cJSON_IsString(item)) {
        ESP_LOGW(TAG, "Key '%s' not found or not string", key);
        cJSON_Delete(json);
        return ESP_ERR_NOT_FOUND;
    }

    strncpy(output, item->valuestring, output_size - 1);
    output[output_size - 1] = '\0';

    cJSON_Delete(json);
    return ESP_OK;
}

esp_err_t ir_json_parse_int(const char *json_str, const char *key, int *output)
{
    if (!json_str || !key || !output) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *json = cJSON_Parse(json_str);
    if (!json) {
        ESP_LOGE(TAG, "Failed to parse JSON");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *item = cJSON_GetObjectItem(json, key);
    if (!item || !cJSON_IsNumber(item)) {
        ESP_LOGW(TAG, "Key '%s' not found or not number", key);
        cJSON_Delete(json);
        return ESP_ERR_NOT_FOUND;
    }

    *output = item->valueint;

    cJSON_Delete(json);
    return ESP_OK;
}

esp_err_t ir_json_parse_array_strings(const char *json_str, const char *key, char output[][32], size_t max_items, size_t *found_items)
{
    if (!json_str || !key || !output || !found_items) {
        return ESP_ERR_INVALID_ARG;
    }

    *found_items = 0;

    cJSON *json = cJSON_Parse(json_str);
    if (!json) {
        ESP_LOGE(TAG, "Failed to parse JSON");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *array = cJSON_GetObjectItem(json, key);
    if (!array || !cJSON_IsArray(array)) {
        ESP_LOGW(TAG, "Key '%s' not found or not array", key);
        cJSON_Delete(json);
        return ESP_ERR_NOT_FOUND;
    }

    int array_size = cJSON_GetArraySize(array);
    *found_items = (array_size < max_items) ? array_size : max_items;

    for (int i = 0; i < *found_items; i++) {
        cJSON *item = cJSON_GetArrayItem(array, i);
        if (cJSON_IsString(item)) {
            strncpy(output[i], item->valuestring, 31);
            output[i][31] = '\0';
        } else {
            output[i][0] = '\0';
        }
    }

    cJSON_Delete(json);
    return ESP_OK;
}

esp_err_t ir_json_create_auth_request(const char *app_key, const char *app_secret, char **output)
{
    if (!app_key || !app_secret || !output) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "app_key", app_key);
    cJSON_AddStringToObject(json, "app_secret", app_secret);

    *output = cJSON_Print(json);
    cJSON_Delete(json);

    if (!*output) {
        ESP_LOGE(TAG, "Failed to create JSON string");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t ir_json_add_auth_fields(const char *base_json, const uint16_t user_id, const char *token, char **output)
{
    if (!token || !output) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *json = NULL;
    
    if (base_json && strlen(base_json) > 0) {
        json = cJSON_Parse(base_json);
    }
    
    if (!json) {
        json = cJSON_CreateObject();
    }

    cJSON_AddNumberToObject(json, "id", user_id);
    cJSON_AddStringToObject(json, "token", token);

    *output = cJSON_Print(json);
    cJSON_Delete(json);

    if (!*output) {
        ESP_LOGE(TAG, "Failed to create JSON string");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t ir_json_parse_hex_data(const char *json_str, const char *key, uint8_t **data, size_t *data_len)
{
    if (!json_str || !key || !data || !data_len) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *json = cJSON_Parse(json_str);
    if (!json) {
        ESP_LOGE(TAG, "Failed to parse JSON");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *item = cJSON_GetObjectItem(json, key);
    if (!item || !cJSON_IsString(item)) {
        ESP_LOGW(TAG, "Key '%s' not found or not string", key);
        cJSON_Delete(json);
        return ESP_ERR_NOT_FOUND;
    }

    const char *hex_str = item->valuestring;
    size_t hex_len = strlen(hex_str);
    
    if (hex_len % 2 != 0) {
        ESP_LOGE(TAG, "Invalid hex string length");
        cJSON_Delete(json);
        return ESP_ERR_INVALID_ARG;
    }

    *data_len = hex_len / 2;
    *data = (uint8_t *)heap_caps_malloc(*data_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!*data) *data = (uint8_t *)malloc(*data_len);
    if (!*data) {
        ESP_LOGE(TAG, "Failed to allocate memory");
        cJSON_Delete(json);
        return ESP_ERR_NO_MEM;
    }

    for (size_t i = 0; i < *data_len; i++) {
        unsigned int byte;
        if (sscanf(hex_str + i * 2, "%2x", &byte) != 1) {
            ESP_LOGE(TAG, "Failed to parse hex byte at position %d", i);
            free(*data);
            *data = NULL;
            cJSON_Delete(json);
            return ESP_ERR_INVALID_ARG;
        }
        (*data)[i] = (uint8_t)byte;
    }

    cJSON_Delete(json);
    return ESP_OK;
}

void ir_json_free_string(char *json_str)
{
    if (json_str) {
        free(json_str);
    }
}

void ir_json_free_data(uint8_t *data)
{
    if (data) {
        free(data);
    }
} 