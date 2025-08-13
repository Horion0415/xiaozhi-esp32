/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "ir_cache";

#define NVS_NAMESPACE "ir_cache"
#define MAX_CACHE_KEY_LEN 64
#define MAX_CACHE_VALUE_LEN 512
#define CACHE_EXPIRE_TIME_SEC (24 * 3600)

typedef struct {
    char key[MAX_CACHE_KEY_LEN];
    char value[MAX_CACHE_VALUE_LEN];
    uint64_t expire_time;
    bool valid;
} cache_entry_t;

static cache_entry_t g_cache_entries[16];
static bool g_cache_initialized = false;

static void cache_generate_key(const char *prefix, const char *brand, const char *model, char *key, size_t key_size)
{
    snprintf(key, key_size, "%s_%s_%s", prefix ? prefix : "", brand ? brand : "", model ? model : "");
    
    for (int i = 0; key[i]; i++) {
        if (key[i] == ' ' || key[i] == '-' || key[i] == '/') {
            key[i] = '_';
        }
    }
}

esp_err_t ir_cache_init(void)
{
    if (g_cache_initialized) {
        return ESP_OK;
    }

    memset(g_cache_entries, 0, sizeof(g_cache_entries));
    
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err == ESP_OK) {
        for (int i = 0; i < 16; i++) {
            char nvs_key[16];
            snprintf(nvs_key, sizeof(nvs_key), "entry_%d", i);
            
            size_t required_size = sizeof(cache_entry_t);
            err = nvs_get_blob(nvs_handle, nvs_key, &g_cache_entries[i], &required_size);
            if (err == ESP_OK) {
                uint64_t current_time = esp_timer_get_time() / 1000000;
                if (g_cache_entries[i].expire_time < current_time) {
                    g_cache_entries[i].valid = false;
                }
            }
        }
        nvs_close(nvs_handle);
    }

    g_cache_initialized = true;
    ESP_LOGI(TAG, "Cache initialized");
    return ESP_OK;
}

esp_err_t ir_cache_deinit(void)
{
    if (!g_cache_initialized) {
        return ESP_OK;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        for (int i = 0; i < 16; i++) {
            if (g_cache_entries[i].valid) {
                char nvs_key[16];
                snprintf(nvs_key, sizeof(nvs_key), "entry_%d", i);
                nvs_set_blob(nvs_handle, nvs_key, &g_cache_entries[i], sizeof(cache_entry_t));
            }
        }
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
    }

    g_cache_initialized = false;
    ESP_LOGI(TAG, "Cache deinitialized");
    return ESP_OK;
}

esp_err_t ir_cache_get(const char *prefix, const char *brand, const char *model, char *value, size_t value_size)
{
    if (!g_cache_initialized || !value) {
        return ESP_ERR_INVALID_STATE;
    }

    char key[MAX_CACHE_KEY_LEN];
    cache_generate_key(prefix, brand, model, key, sizeof(key));

    uint64_t current_time = esp_timer_get_time() / 1000000;

    for (int i = 0; i < 16; i++) {
        if (g_cache_entries[i].valid && strcmp(g_cache_entries[i].key, key) == 0) {
            if (g_cache_entries[i].expire_time > current_time) {
                strncpy(value, g_cache_entries[i].value, value_size - 1);
                value[value_size - 1] = '\0';
                ESP_LOGI(TAG, "Cache hit for key: %s", key);
                return ESP_OK;
            } else {
                g_cache_entries[i].valid = false;
                ESP_LOGI(TAG, "Cache expired for key: %s", key);
                break;
            }
        }
    }

    return ESP_ERR_NOT_FOUND;
}

esp_err_t ir_cache_set(const char *prefix, const char *brand, const char *model, const char *value)
{
    if (!g_cache_initialized || !value) {
        return ESP_ERR_INVALID_STATE;
    }

    char key[MAX_CACHE_KEY_LEN];
    cache_generate_key(prefix, brand, model, key, sizeof(key));

    int empty_slot = -1;
    int oldest_slot = 0;
    uint64_t oldest_time = g_cache_entries[0].expire_time;

    for (int i = 0; i < 16; i++) {
        if (!g_cache_entries[i].valid) {
            empty_slot = i;
            break;
        }
        if (g_cache_entries[i].expire_time < oldest_time) {
            oldest_time = g_cache_entries[i].expire_time;
            oldest_slot = i;
        }
    }

    int slot = (empty_slot >= 0) ? empty_slot : oldest_slot;

    strncpy(g_cache_entries[slot].key, key, sizeof(g_cache_entries[slot].key) - 1);
    strncpy(g_cache_entries[slot].value, value, sizeof(g_cache_entries[slot].value) - 1);
    g_cache_entries[slot].expire_time = esp_timer_get_time() / 1000000 + CACHE_EXPIRE_TIME_SEC;
    g_cache_entries[slot].valid = true;

    ESP_LOGI(TAG, "Cache set for key: %s (slot %d)", key, slot);
    return ESP_OK;
}

esp_err_t ir_cache_clear(void)
{
    if (!g_cache_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(g_cache_entries, 0, sizeof(g_cache_entries));

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err == ESP_OK) {
        nvs_erase_all(nvs_handle);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
    }

    ESP_LOGI(TAG, "Cache cleared");
    return ESP_OK;
}

esp_err_t ir_cache_get_stats(size_t *total_entries, size_t *valid_entries, size_t *expired_entries)
{
    if (!g_cache_initialized || !total_entries || !valid_entries || !expired_entries) {
        return ESP_ERR_INVALID_ARG;
    }

    *total_entries = 16;
    *valid_entries = 0;
    *expired_entries = 0;

    uint64_t current_time = esp_timer_get_time() / 1000000;

    for (int i = 0; i < 16; i++) {
        if (g_cache_entries[i].valid) {
            if (g_cache_entries[i].expire_time > current_time) {
                (*valid_entries)++;
            } else {
                (*expired_entries)++;
            }
        }
    }

    return ESP_OK;
} 