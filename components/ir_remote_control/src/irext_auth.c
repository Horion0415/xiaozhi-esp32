/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "cJSON.h"
#include "irext_auth.h"
#include "esp_heap_caps.h"

static const char *TAG = "irext_auth";

// Structure to hold HTTP response data
typedef struct {
    char *buffer;
    size_t buffer_size;
    size_t data_len;
} http_response_t;

// HTTP event handler to accumulate response data
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_response_t *response = (http_response_t *)evt->user_data;
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (response && response->buffer && evt->data_len > 0) {
                size_t copy_len = evt->data_len;
                if (response->data_len + copy_len >= response->buffer_size) {
                    copy_len = response->buffer_size - response->data_len - 1;
                }
                if (copy_len > 0) {
                    memcpy(response->buffer + response->data_len, evt->data, copy_len);
                    response->data_len += copy_len;
                    response->buffer[response->data_len] = '\0';
                }
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}


#define NVS_NAMESPACE "irext_auth"
#define MAX_TOKEN_LEN 256

static irext_credentials_t g_credentials = {0};
static ir_irext_config_t g_config = {0};
static bool g_auth_initialized = false;

esp_err_t irext_auth_init(const ir_irext_config_t *config)
{
    if (!config || !config->app_key || !config->app_secret) {
        ESP_LOGE(TAG, "Invalid config");
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(&g_config, config, sizeof(ir_irext_config_t));
    g_auth_initialized = true;

    if (config->cache_token) {
        nvs_handle_t nvs_handle;
        esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
        if (err == ESP_OK) {
            size_t required_size = sizeof(irext_credentials_t);
            err = nvs_get_blob(nvs_handle, "credentials", &g_credentials, &required_size);
            if (err == ESP_OK && g_credentials.is_valid) {
                ESP_LOGI(TAG, "Loaded cached credentials");
            }
            nvs_close(nvs_handle);
        }
    }

    if (config->auto_login && !irext_auth_is_valid()) {
        return irext_auth_login();
    }

    ESP_LOGI(TAG, "Auth initialized");
    return ESP_OK;
}

esp_err_t irext_auth_deinit(void)
{
    g_auth_initialized = false;
    memset(&g_credentials, 0, sizeof(irext_credentials_t));
    memset(&g_config, 0, sizeof(ir_irext_config_t));
    ESP_LOGI(TAG, "Auth deinitialized");
    return ESP_OK;
}

static esp_err_t save_credentials_to_nvs(void)
{
    if (!g_config.cache_token) return ESP_OK;

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) return err;

    err = nvs_set_blob(nvs_handle, "credentials", &g_credentials, sizeof(irext_credentials_t));
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
    }
    nvs_close(nvs_handle);
    return err;
}

esp_err_t irext_auth_login(void)
{
    if (!g_auth_initialized) {
        ESP_LOGE(TAG, "Auth not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "=== Starting IRext Authentication ===");
    ESP_LOGI(TAG, "Server URL: %s", g_config.server_url);
    ESP_LOGI(TAG, "APP Key: %.10s...", g_config.app_key);
    ESP_LOGI(TAG, "APP Secret: %.10s...", g_config.app_secret);
    ESP_LOGI(TAG, "Login to IRext service");

    char url[256];
    snprintf(url, sizeof(url), "%s/app/app_login", g_config.server_url);
    // Debug log: show final URL
    ESP_LOGI(TAG, "Request URL: %s", url);

    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "appKey", g_config.app_key);
    cJSON_AddStringToObject(json, "appSecret", g_config.app_secret);
    cJSON_AddNumberToObject(json, "appType", 2);
    char *json_string = cJSON_Print(json);
    // Debug log: show JSON body
    ESP_LOGI(TAG, "Request Body: %s", json_string);

    // Prepare response container
    http_response_t response = {
        .buffer = (char *)heap_caps_malloc(2048, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
        .buffer_size = 2048,
        .data_len = 0
    };
    if (!response.buffer) {
        response.buffer = (char *)malloc(2048);
    }
    int response_len = 0;

    if (!response.buffer) {
        ESP_LOGE(TAG, "Failed to allocate response buffer");
        free(json_string);
        cJSON_Delete(json);
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = g_config.timeout_ms,
        .event_handler = http_event_handler,
        .user_data = &response,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to create HTTP client");
        free(response.buffer);
        free(json_string);
        cJSON_Delete(json);
        return ESP_ERR_NO_MEM;
    }
    
    esp_http_client_set_header(client, "Content-Type", "application/json");
    #ifdef CONFIG_IR_USER_LANG
    esp_http_client_set_header(client, "user-lang", CONFIG_IR_USER_LANG);
    ESP_LOGI(TAG, "Set user-lang header: %s", CONFIG_IR_USER_LANG);
    #endif
    esp_http_client_set_post_field(client, json_string, strlen(json_string));

    ESP_LOGI(TAG, "Sending HTTP POST request...");
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        response_len = response.data_len;
        ESP_LOGI(TAG, "HTTP status=%d, response_len=%d", status_code, response_len);
        ESP_LOGI(TAG, "Response snippet: %.*s", (response_len < 512 ? response_len : 512), response.buffer);

        if (status_code == 200 && response_len > 0) {
            cJSON *response_json = cJSON_Parse(response.buffer);
            if (response_json) {
                cJSON *status_obj = cJSON_GetObjectItem(response_json, "status");
                if (status_obj) {
                    cJSON *code_item = cJSON_GetObjectItem(status_obj, "code");
                    if (code_item && cJSON_IsNumber(code_item) && code_item->valueint == 0) {
                        cJSON *entity = cJSON_GetObjectItem(response_json, "entity");
                        if (entity) {
                            cJSON *id_item = cJSON_GetObjectItem(entity, "id");
                            cJSON *token_item = cJSON_GetObjectItem(entity, "token");
                            
                            if (id_item && token_item && cJSON_IsNumber(id_item) && cJSON_IsString(token_item)) {
                                g_credentials.user_id = (uint16_t)id_item->valueint;
                                strncpy(g_credentials.token, token_item->valuestring, sizeof(g_credentials.token) - 1);
                                g_credentials.expires_at = esp_timer_get_time() / 1000000 + (24 * 3600);
                                g_credentials.is_valid = true;

                                save_credentials_to_nvs();
                                ESP_LOGI(TAG, "Login successful");
                                err = ESP_OK;
                            } else {
                                ESP_LOGE(TAG, "Invalid login response entity");
                                err = ESP_ERR_INVALID_RESPONSE;
                            }
                        } else {
                            ESP_LOGE(TAG, "Missing entity in login response");
                            err = ESP_ERR_INVALID_RESPONSE;
                        }
                    } else {
                        int error_code = code_item ? code_item->valueint : -1;
                        ESP_LOGE(TAG, "Login failed with status code %d", error_code);
                        
                        cJSON *message_item = cJSON_GetObjectItem(status_obj, "message");
                        if (message_item && cJSON_IsString(message_item)) {
                            ESP_LOGE(TAG, "Error message: %s", message_item->valuestring);
                        }
                        
                        switch(error_code) {
                            case 1:
                                ESP_LOGE(TAG, "Authentication failed - Invalid APP_KEY or APP_SECRET");
                                break;
                            case 2:
                                ESP_LOGE(TAG, "Parameter error - Check request format");
                                break;
                            case 3:
                                ESP_LOGE(TAG, "Permission denied - Check account permissions");
                                break;
                            default:
                                ESP_LOGE(TAG, "Unknown error code: %d", error_code);
                                break;
                        }
                        err = ESP_FAIL;
                    }
                } else {
                    ESP_LOGE(TAG, "Invalid login response format - missing status");
                    err = ESP_ERR_INVALID_RESPONSE;
                }
                cJSON_Delete(response_json);
            } else {
                ESP_LOGE(TAG, "Failed to parse login response");
                err = ESP_ERR_INVALID_RESPONSE;
            }
        } else {
            ESP_LOGE(TAG, "Login HTTP error: %d", status_code);
            ESP_LOGE(TAG, "Response content: %.*s", (response_len < 200 ? response_len : 200), response.buffer);
            
            // 分析HTTP错误码
            switch(status_code) {
                case 404:
                    ESP_LOGE(TAG, "Server endpoint not found - Check server URL");
                    break;
                case 403:
                    ESP_LOGE(TAG, "Access forbidden - Check APP credentials");
                    break;
                case 500:
                    ESP_LOGE(TAG, "Server internal error");
                    break;
                case 0:
                    ESP_LOGE(TAG, "Network connection failed - Check WiFi and DNS");
                    break;
                default:
                    ESP_LOGE(TAG, "HTTP error %d", status_code);
                    break;
            }
            err = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "Login HTTP request failed: %s", esp_err_to_name(err));
        
        // 分析具体的网络错误
        switch(err) {
            case ESP_ERR_HTTP_CONNECT:
                ESP_LOGE(TAG, "Cannot connect to server - Check network and server URL");
                break;
            case ESP_ERR_HTTP_WRITE_DATA:
                ESP_LOGE(TAG, "Failed to send data - Network issue");
                break;
            case ESP_ERR_HTTP_FETCH_HEADER:
                ESP_LOGE(TAG, "Failed to receive header - Server issue");
                break;
            case ESP_ERR_TIMEOUT:
                ESP_LOGE(TAG, "Request timeout - Server too slow or network issue");
                break;
            default:
                ESP_LOGE(TAG, "Network error: %s", esp_err_to_name(err));
                break;
        }
    }

    esp_http_client_cleanup(client);
    free(response.buffer);
    free(json_string);
    cJSON_Delete(json);

    return err;
}

bool irext_auth_is_valid(void)
{
    if (!g_credentials.is_valid) return false;
    
    uint64_t current_time = esp_timer_get_time() / 1000000;
    return current_time < g_credentials.expires_at;
}

esp_err_t irext_auth_get_credentials(irext_credentials_t *credentials)
{
    if (!credentials) return ESP_ERR_INVALID_ARG;
    if (!irext_auth_is_valid()) return ESP_ERR_INVALID_STATE;

    memcpy(credentials, &g_credentials, sizeof(irext_credentials_t));
    return ESP_OK;
}

esp_err_t irext_auth_refresh_if_needed(void)
{
    if (irext_auth_is_valid()) return ESP_OK;
    
    ESP_LOGI(TAG, "Token expired, refreshing");
    return irext_auth_login();
}

esp_err_t irext_auth_clear_cache(void)
{
    memset(&g_credentials, 0, sizeof(irext_credentials_t));
    
    if (g_config.cache_token) {
        nvs_handle_t nvs_handle;
        esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
        if (err == ESP_OK) {
            nvs_erase_key(nvs_handle, "credentials");
            nvs_commit(nvs_handle);
            nvs_close(nvs_handle);
        }
    }
    
    ESP_LOGI(TAG, "Cache cleared");
    return ESP_OK;
}

esp_err_t irext_auth_build_request_body(const char *base_json, char *output_buffer, size_t buffer_size)
{
    if (!base_json || !output_buffer || buffer_size == 0 || !irext_auth_is_valid()) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *json = cJSON_Parse(base_json);
    if (!json) {
        json = cJSON_CreateObject();
        if (!json) {
            ESP_LOGE(TAG, "Failed to create JSON object");
            return ESP_ERR_NO_MEM;
        }
    }

    cJSON_AddNumberToObject(json, "id", g_credentials.user_id);
    cJSON_AddStringToObject(json, "token", g_credentials.token);

    char *json_string = cJSON_Print(json);
    if (!json_string) {
        ESP_LOGE(TAG, "Failed to print JSON");
        cJSON_Delete(json);
        return ESP_ERR_NO_MEM;
    }

    size_t json_len = strlen(json_string);
    if (json_len >= buffer_size) {
        ESP_LOGE(TAG, "Output buffer too small: need %d, have %d", json_len + 1, buffer_size);
        free(json_string);
        cJSON_Delete(json);
        return ESP_ERR_NO_MEM;
    }

    strncpy(output_buffer, json_string, buffer_size - 1);
    output_buffer[buffer_size - 1] = '\0';
    
    free(json_string);
    cJSON_Delete(json);

    return ESP_OK;
} 