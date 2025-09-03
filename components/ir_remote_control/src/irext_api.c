/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <stdlib.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "irext_api.h"
#include "irext_auth.h"
#include "ir_transmitter.h"
#include <strings.h>
#include "esp_heap_caps.h"
#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "irext_api";

// Forward declarations for category mapping helpers
static uint32_t to_server_category_id(ir_device_category_t category);
static void ensure_category_mapping_loaded(void);

// Structure to hold HTTP response during streaming
typedef struct {
    char *buffer;
    size_t buffer_size;
    size_t data_len;
} http_response_t;

// http_event_handler accumulates incoming data into the provided buffer
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_response_t *response = (http_response_t *)evt->user_data;
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (response && evt->data_len > 0) {
                size_t need = response->data_len + evt->data_len + 1;
                if (!response->buffer) {
                    response->buffer_size = need < 16384 ? 16384 : need;
                    char *buf = (char *)heap_caps_malloc(response->buffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                    if (!buf) buf = (char *)malloc(response->buffer_size);
                    if (!buf) return ESP_ERR_NO_MEM;
                    response->buffer = buf;
                    response->data_len = 0;
                    response->buffer[0] = '\0';
                }
                if (need > response->buffer_size) {
                    size_t new_cap = response->buffer_size * 2;
                    if (new_cap < need) new_cap = need;
                    char *new_buf = (char *)heap_caps_malloc(new_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                    if (!new_buf) new_buf = (char *)malloc(new_cap);
                    if (!new_buf) {
                        ESP_LOGE(TAG, "HTTP buffer expand failed: need=%u", (unsigned)need);
                        return ESP_ERR_NO_MEM;
                    }
                    // copy existing
                    if (response->buffer && response->data_len > 0) {
                        memcpy(new_buf, response->buffer, response->data_len);
                    }
                    // free old and switch
                    if (response->buffer) free(response->buffer);
                    response->buffer = new_buf;
                    response->buffer_size = new_cap;
                }
                memcpy(response->buffer + response->data_len, evt->data, evt->data_len);
                response->data_len += evt->data_len;
                response->buffer[response->data_len] = '\0';
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}


static ir_irext_config_t g_config = {0};
static bool g_api_initialized = false;
static esp_err_t g_last_error = ESP_OK;

esp_err_t irext_api_init(const ir_irext_config_t *config)
{
    if (!config) {
        ESP_LOGE(TAG, "Invalid config");
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(&g_config, config, sizeof(ir_irext_config_t));
    g_api_initialized = true;
    g_last_error = ESP_OK;

    ESP_LOGI(TAG, "API client initialized");
    return ESP_OK;
}

esp_err_t irext_api_deinit(void)
{
    g_api_initialized = false;
    memset(&g_config, 0, sizeof(ir_irext_config_t));
    g_last_error = ESP_OK;
    ESP_LOGI(TAG, "API client deinitialized");
    return ESP_OK;
}

static esp_err_t http_post_request(const char *endpoint, const char *body, char **response, size_t *response_len)
{
    if (!g_api_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    char url[512];
    size_t base_len = strlen(g_config.server_url);
    bool base_has_slash = base_len > 0 && g_config.server_url[base_len - 1] == '/';
    bool ep_has_slash = endpoint && endpoint[0] == '/';
    if (base_has_slash && ep_has_slash) {
        snprintf(url, sizeof(url), "%.*s%s", (int)(base_len - 1), g_config.server_url, endpoint);
    } else if (!base_has_slash && !ep_has_slash) {
        snprintf(url, sizeof(url), "%s/%s", g_config.server_url, endpoint);
    } else {
        snprintf(url, sizeof(url), "%s%s", g_config.server_url, endpoint);
    }

    http_response_t response_ctx = {
        .buffer = NULL,
        .buffer_size = 0,
        .data_len = 0
    };

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = g_config.timeout_ms,
        .event_handler = http_event_handler,
        .user_data = &response_ctx,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/json");
#if defined(CONFIG_IR_USER_LANG)
    esp_http_client_set_header(client, "user-lang", CONFIG_IR_USER_LANG);
#endif
    esp_http_client_set_post_field(client, body, strlen(body));

    *response = NULL;
    *response_len = 0;

    ESP_LOGD(TAG, "HTTP POST %s url=%s body_len=%d", endpoint, url, (int)strlen(body));

    esp_err_t err = ESP_FAIL;
    for (int attempt = 0; attempt <= CONFIG_IR_RETRY_COUNT; attempt++) {
        response_ctx.data_len = 0;
        err = esp_http_client_perform(client);
        if (err == ESP_OK) {
            int status_code = esp_http_client_get_status_code(client);
            if (status_code == 200) {
                *response = response_ctx.buffer ? response_ctx.buffer : strdup("");
                *response_len = response_ctx.data_len;
                break;
            } else {
                ESP_LOGE(TAG, "HTTP error %d for %s", status_code, endpoint);
                err = ESP_FAIL;
            }
        } else {
            ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
        }
        if (err != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }

    if (err != ESP_OK) {
        if (response_ctx.buffer) free(response_ctx.buffer);
        *response = NULL;
        *response_len = 0;
    }

    esp_http_client_cleanup(client);
    g_last_error = err;
    return err;
}

esp_err_t irext_api_send_ac_command(const ir_device_info_t *device_info, const ir_ac_status_t *ac_status)
{
    if (!device_info || !ac_status) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = irext_auth_refresh_if_needed();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Auth refresh failed");
        return err;
    }

    ir_ac_status_t status_adj = *ac_status;
    ir_ac_parameters_t params;
    err = irext_api_get_ac_parameters(device_info->model_id, ac_status->mode, &params);
    if (err == ESP_OK) {
        if (status_adj.temperature < params.temp_min) status_adj.temperature = params.temp_min;
        if (status_adj.temperature > params.temp_max) status_adj.temperature = params.temp_max;
    }

#if defined(CONFIG_IR_DECODE_RETRY_COUNT)
    const int max_attempts = (CONFIG_IR_DECODE_RETRY_COUNT > 0) ? (CONFIG_IR_DECODE_RETRY_COUNT + 1) : 1;
#else
    const int max_attempts = 3;
#endif
    esp_err_t last_err = ESP_FAIL;
    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        uint32_t *ir_data = NULL;
        size_t data_length = 0;
        err = irext_api_decode_command(device_info->model_id, 1, &status_adj, 0, 0, &ir_data, &data_length);
        if (err != ESP_OK) {
            last_err = err;
            vTaskDelay(pdMS_TO_TICKS(150));
            continue;
        }
        if (!ir_data || data_length < 2) {
            if (ir_data) free(ir_data);
            last_err = ESP_ERR_INVALID_RESPONSE;
            vTaskDelay(pdMS_TO_TICKS(150));
            continue;
        }
        err = ir_transmitter_send_timing_data(ir_data, data_length);
        free(ir_data);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "AC command sent successfully");
            return ESP_OK;
        } else {
            ESP_LOGE(TAG, "IR transmission failed: %s", esp_err_to_name(err));
            last_err = err;
            vTaskDelay(pdMS_TO_TICKS(150));
        }
    }

    return last_err;
}

esp_err_t irext_api_send_key_command(const ir_device_info_t *device_info, uint32_t key_code)
{
    if (!device_info) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = irext_auth_refresh_if_needed();
    if (err != ESP_OK) return err;

#if defined(CONFIG_IR_DECODE_RETRY_COUNT)
    const int max_attempts = (CONFIG_IR_DECODE_RETRY_COUNT > 0) ? (CONFIG_IR_DECODE_RETRY_COUNT + 1) : 1;
#else
    const int max_attempts = 3;
#endif
    esp_err_t last_err = ESP_FAIL;
    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        uint32_t *ir_data = NULL;
        size_t data_length = 0;
        err = irext_api_decode_command(device_info->model_id, key_code, NULL, 0, 0, &ir_data, &data_length);
        if (err != ESP_OK) {
            last_err = err;
            vTaskDelay(pdMS_TO_TICKS(150));
            continue;
        }
        if (!ir_data || data_length < 2) {
            if (ir_data) free(ir_data);
            last_err = ESP_ERR_INVALID_RESPONSE;
            vTaskDelay(pdMS_TO_TICKS(150));
            continue;
        }
        err = ir_transmitter_send_timing_data(ir_data, data_length);
        free(ir_data);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Key command sent successfully");
            return ESP_OK;
        } else {
            ESP_LOGE(TAG, "IR transmission failed: %s", esp_err_to_name(err));
            last_err = err;
            vTaskDelay(pdMS_TO_TICKS(150));
        }
    }

    return last_err;
}

esp_err_t irext_api_get_brands(ir_device_category_t category, char brands[][IR_MAX_BRAND_NAME_LEN], size_t max_brands, size_t *found_count)
{
    if (!brands || !found_count) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = irext_auth_refresh_if_needed();
    if (err != ESP_OK) return err;

    size_t total = 0;
    int from = 0;
    const int page = 20;

    while (total < max_brands) {
        cJSON *json = cJSON_CreateObject();
        cJSON_AddNumberToObject(json, "categoryId", to_server_category_id(category));
        cJSON_AddNumberToObject(json, "from", from);
        cJSON_AddNumberToObject(json, "count", page);
        char *base_json = cJSON_Print(json);
        cJSON_Delete(json);

        char auth_body[2048];
        err = irext_auth_build_request_body(base_json, auth_body, sizeof(auth_body));
        free(base_json);
        if (err != ESP_OK) return err;

        char *response;
        size_t response_len;
        err = http_post_request("/indexing/list_brands", auth_body, &response, &response_len);
        if (err != ESP_OK) {
            free(response);
            break;
        }

        size_t added = 0;
        cJSON *response_json = cJSON_Parse(response);
        if (response_json) {
            cJSON *status_obj = cJSON_GetObjectItem(response_json, "status");
            if (status_obj) {
                cJSON *code_item = cJSON_GetObjectItem(status_obj, "code");
                if (code_item && cJSON_IsNumber(code_item) && code_item->valueint == 0) {
                    cJSON *entity = cJSON_GetObjectItem(response_json, "entity");
                    if (cJSON_IsArray(entity)) {
                        int array_size = cJSON_GetArraySize(entity);
                        int take = array_size;
                        for (int i = 0; i < take && total < max_brands; i++) {
                            cJSON *brand_item = cJSON_GetArrayItem(entity, i);
                            if (brand_item) {
                                cJSON *name_item = cJSON_GetObjectItem(brand_item, "name");
                                if (name_item && cJSON_IsString(name_item)) {
                                    strncpy(brands[total], name_item->valuestring, IR_MAX_BRAND_NAME_LEN - 1);
                                    brands[total][IR_MAX_BRAND_NAME_LEN - 1] = '\0';
                                    total++;
                                    added++;
                                }
                            }
                        }
                        if (array_size < page) {
                            cJSON_Delete(response_json);
                            free(response);
                            break;
                        }
                    }
                } else {
                    err = ESP_FAIL;
                }
            } else {
                err = ESP_ERR_INVALID_RESPONSE;
            }
            cJSON_Delete(response_json);
        } else {
            err = ESP_ERR_INVALID_RESPONSE;
        }
        free(response);
        if (added == 0) break;
        from += page;
    }

    *found_count = total;
    return err;
}

esp_err_t irext_api_get_models(ir_device_category_t category, const char *brand, char models[][IR_MAX_MODEL_NAME_LEN], size_t max_models, size_t *found_count)
{
    if (!brand || !models || !found_count) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = irext_auth_refresh_if_needed();
    if (err != ESP_OK) return err;

    uint32_t brand_id = 0;
    err = irext_api_get_brand_id(category, brand, &brand_id);
    if (err != ESP_OK || brand_id == 0) {
        ESP_LOGE(TAG, "Brand '%s' not found in category %d", brand, category);
        return ESP_ERR_NOT_FOUND;
    }

    size_t total = 0;
    int from = 0;
    const int page = 20;

    while (total < max_models) {
        cJSON *json = cJSON_CreateObject();
        cJSON_AddNumberToObject(json, "categoryId", to_server_category_id(category));
        cJSON_AddNumberToObject(json, "brandId", brand_id);
        cJSON_AddNumberToObject(json, "from", from);
        cJSON_AddNumberToObject(json, "count", page);

        char *base_json = cJSON_Print(json);
        cJSON_Delete(json);

        char auth_body[2048];
        err = irext_auth_build_request_body(base_json, auth_body, sizeof(auth_body));
        free(base_json);
        if (err != ESP_OK) return err;

        char *response;
        size_t response_len;
        err = http_post_request("/indexing/list_indexes", auth_body, &response, &response_len);
        if (err != ESP_OK) {
            free(response);
            break;
        }

        size_t added = 0;
        cJSON *response_json = cJSON_Parse(response);
        if (response_json) {
            cJSON *status_obj = cJSON_GetObjectItem(response_json, "status");
            if (status_obj) {
                cJSON *code_item = cJSON_GetObjectItem(status_obj, "code");
                if (code_item && cJSON_IsNumber(code_item) && code_item->valueint == 0) {
                    cJSON *entity = cJSON_GetObjectItem(response_json, "entity");
                    if (cJSON_IsArray(entity)) {
                        int array_size = cJSON_GetArraySize(entity);
                        int take = array_size;
                        for (int i = 0; i < take && total < max_models; i++) {
                            cJSON *index_item = cJSON_GetArrayItem(entity, i);
                            if (index_item) {
                                cJSON *remote_item = cJSON_GetObjectItem(index_item, "remote");
                                if (remote_item && cJSON_IsString(remote_item)) {
                                    strncpy(models[total], remote_item->valuestring, IR_MAX_MODEL_NAME_LEN - 1);
                                    models[total][IR_MAX_MODEL_NAME_LEN - 1] = '\0';
                                    total++;
                                    added++;
                                }
                            }
                        }
                        if (array_size < page) {
                            cJSON_Delete(response_json);
                            free(response);
                            break;
                        }
                    }
                } else {
                    err = ESP_FAIL;
                }
            } else {
                err = ESP_ERR_INVALID_RESPONSE;
            }
            cJSON_Delete(response_json);
        } else {
            err = ESP_ERR_INVALID_RESPONSE;
        }
        free(response);
        if (added == 0) break;
        from += page;
    }

    *found_count = total;
    return err;
}

esp_err_t irext_api_search_brands(ir_device_category_t category, const char *pattern, char brands[][IR_MAX_BRAND_NAME_LEN], size_t max_brands, size_t *found_count)
{
    esp_err_t err = irext_api_get_brands(category, brands, max_brands, found_count);
    if (err != ESP_OK) return err;

    size_t filtered_count = 0;
    for (size_t i = 0; i < *found_count; i++) {
        if (strstr(brands[i], pattern) != NULL) {
            if (filtered_count != i) {
                strcpy(brands[filtered_count], brands[i]);
            }
            filtered_count++;
        }
    }

    *found_count = filtered_count;
    ESP_LOGI(TAG, "Found %d brands matching '%s'", *found_count, pattern);
    return ESP_OK;
}

esp_err_t irext_api_search_models(ir_device_category_t category, const char *brand, const char *pattern, char models[][IR_MAX_MODEL_NAME_LEN], size_t max_models, size_t *found_count)
{
    esp_err_t err = irext_api_get_models(category, brand, models, max_models, found_count);
    if (err != ESP_OK) return err;

    size_t filtered_count = 0;
    for (size_t i = 0; i < *found_count; i++) {
        if (strstr(models[i], pattern) != NULL) {
            if (filtered_count != i) {
                strcpy(models[filtered_count], models[i]);
            }
            filtered_count++;
        }
    }

    *found_count = filtered_count;
    ESP_LOGI(TAG, "Found %d models matching '%s'", *found_count, pattern);
    return ESP_OK;
}

esp_err_t irext_api_find_device(ir_device_category_t category, const char *brand, const char *model, uint32_t *brand_id, uint32_t *model_id)
{
    if (!brand || !model || !brand_id || !model_id) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGD(TAG, "Finding device: %s %s", brand, model);

    esp_err_t err = irext_auth_refresh_if_needed();
    if (err != ESP_OK) return err;

    ESP_LOGD(TAG, "Finding device 1: %s %s", brand, model);

    uint32_t tmp_brand_id = 0;
    err = irext_api_get_brand_id(category, brand, &tmp_brand_id);
    if (err != ESP_OK || tmp_brand_id == 0) {
        ESP_LOGW(TAG, "Brand '%s' not found in category %d, fallback to all categories", brand, category);
        // Fallback: iterate categories to locate the brand/model
        ir_category_t cats[32]; size_t cat_count = 0;
        if (irext_api_get_categories(cats, 32, &cat_count) == ESP_OK) {
            for (size_t ci = 0; ci < cat_count; ci++) {
                tmp_brand_id = 0;
                if (irext_api_get_brand_id((ir_device_category_t)cats[ci].id, brand, &tmp_brand_id) != ESP_OK || tmp_brand_id == 0) {
                    continue;
                }
                int from = 0; const int page = 100;
                for (;;) {
                    cJSON *json = cJSON_CreateObject();
                    cJSON_AddNumberToObject(json, "categoryId", cats[ci].id);
                    cJSON_AddNumberToObject(json, "brandId", tmp_brand_id);
                    cJSON_AddNumberToObject(json, "from", from);
                    cJSON_AddNumberToObject(json, "count", page);
                    char *base_json = cJSON_Print(json);
                    cJSON_Delete(json);
                    if (!base_json) { return ESP_ERR_NO_MEM; }
                    char auth_body[4096];
                    err = irext_auth_build_request_body(base_json, auth_body, sizeof(auth_body));
                    free(base_json);
                    if (err != ESP_OK) return err;
                    char *response = NULL; size_t response_len = 0;
                    err = http_post_request("/indexing/list_indexes", auth_body, &response, &response_len);
                    if (err != ESP_OK || !response) return err != ESP_OK ? err : ESP_ERR_INVALID_RESPONSE;
                    cJSON *response_json = cJSON_Parse(response);
                    if (!response_json) { free(response); return ESP_ERR_INVALID_RESPONSE; }
                    cJSON *status_obj = cJSON_GetObjectItem(response_json, "status");
                    cJSON *code_item = status_obj ? cJSON_GetObjectItem(status_obj, "code") : NULL;
                    if (!code_item || !cJSON_IsNumber(code_item) || code_item->valueint != 0) {
                        cJSON_Delete(response_json); free(response); return ESP_ERR_INVALID_RESPONSE;
                    }
                    cJSON *entity = cJSON_GetObjectItem(response_json, "entity");
                    int array_size = cJSON_IsArray(entity) ? cJSON_GetArraySize(entity) : 0;
                    for (int i = 0; i < array_size; i++) {
                        cJSON *index_item = cJSON_GetArrayItem(entity, i);
                        if (!index_item) continue;
                        cJSON *remote_item = cJSON_GetObjectItem(index_item, "remote");
                        cJSON *id_item = cJSON_GetObjectItem(index_item, "id");
                        cJSON *brand_id_item = cJSON_GetObjectItem(index_item, "brandId");
                        if (remote_item && cJSON_IsString(remote_item) && id_item && cJSON_IsNumber(id_item)) {
                            if (strcmp(remote_item->valuestring, model) == 0) {
                                *model_id = id_item->valueint;
                                *brand_id = brand_id_item && cJSON_IsNumber(brand_id_item) ? brand_id_item->valueint : tmp_brand_id;
                                cJSON_Delete(response_json); free(response);
                                ESP_LOGI(TAG, "Found device across categories: cat=%d %s %s (bid=%d, mid=%d)", (int)cats[ci].id, brand, model, *brand_id, *model_id);
                                return ESP_OK;
                            }
                        }
                    }
                    cJSON_Delete(response_json); free(response);
                    if (array_size < page) break;
                    from += page;
                }
            }
        }
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGD(TAG, "Resolved brandId=%d for '%s'", tmp_brand_id, brand);

    int from = 0;
    const int page = 100;
    for (;;) {
        ESP_LOGD(TAG, "Indexes page: category=%d brandId=%d from=%d count=%d targetRemote='%s'", category, tmp_brand_id, from, page, model);
        cJSON *json = cJSON_CreateObject();
        cJSON_AddNumberToObject(json, "categoryId", to_server_category_id(category));
        cJSON_AddNumberToObject(json, "brandId", tmp_brand_id);
        cJSON_AddNumberToObject(json, "from", from);
        cJSON_AddNumberToObject(json, "count", page);

        char *base_json = cJSON_Print(json);
        cJSON_Delete(json);

        if (!base_json) {
            ESP_LOGE(TAG, "Failed to print JSON");
            return ESP_ERR_NO_MEM;
        }

        size_t auth_body_size = 4096;
        char *auth_body = (char *)heap_caps_malloc(auth_body_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!auth_body) auth_body = (char *)malloc(auth_body_size);
        if (!auth_body) {
            ESP_LOGE(TAG, "Failed to allocate auth body buffer");
            free(base_json);
            return ESP_ERR_NO_MEM;
        }

        err = irext_auth_build_request_body(base_json, auth_body, auth_body_size);
        free(base_json);

        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to build auth request body: %s", esp_err_to_name(err));
            free(auth_body);
            return err;
        }

        char *response = NULL;
        size_t response_len = 0;
        err = http_post_request("/indexing/list_indexes", auth_body, &response, &response_len);

        if (err != ESP_OK) {
            ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
            free(auth_body);
            return err;
        }

        if (!response) {
            ESP_LOGE(TAG, "Received null response");
            free(auth_body);
            return ESP_ERR_INVALID_RESPONSE;
        }

        cJSON *response_json = cJSON_Parse(response);
        if (!response_json) {
            ESP_LOGE(TAG, "Failed to parse response JSON");
            free(response);
            return ESP_ERR_INVALID_RESPONSE;
        }

        cJSON *status_obj = cJSON_GetObjectItem(response_json, "status");
        if (!status_obj) {
            ESP_LOGE(TAG, "No status object in response");
            cJSON_Delete(response_json);
            free(response);
            return ESP_ERR_INVALID_RESPONSE;
        }

        cJSON *code_item = cJSON_GetObjectItem(status_obj, "code");
        if (!code_item || !cJSON_IsNumber(code_item)) {
            ESP_LOGE(TAG, "Invalid status code in response");
            cJSON_Delete(response_json);
            free(response);
            return ESP_ERR_INVALID_RESPONSE;
        }

        if (code_item->valueint != 0) {
            ESP_LOGE(TAG, "API returned error code: %d", code_item->valueint);
            cJSON_Delete(response_json);
            free(response);
            return ESP_FAIL;
        }

        cJSON *entity = cJSON_GetObjectItem(response_json, "entity");
        if (!entity || !cJSON_IsArray(entity)) {
            ESP_LOGE(TAG, "Invalid entity array in response");
            cJSON_Delete(response_json);
            free(response);
            return ESP_ERR_INVALID_RESPONSE;
        }

        int array_size = cJSON_GetArraySize(entity);
        ESP_LOGD(TAG, "Indexes parsed=%d (from=%d)", array_size, from);
        for (int i = 0; i < array_size; i++) {
            cJSON *index_item = cJSON_GetArrayItem(entity, i);
            if (!index_item) continue;

            cJSON *remote_item = cJSON_GetObjectItem(index_item, "remote");
            cJSON *id_item = cJSON_GetObjectItem(index_item, "id");
            cJSON *brand_id_item = cJSON_GetObjectItem(index_item, "brandId");

            if (remote_item && cJSON_IsString(remote_item) && remote_item->valuestring &&
                strcmp(remote_item->valuestring, model) == 0) {

                *model_id = 0;
                *brand_id = 1;

                if (id_item && cJSON_IsNumber(id_item)) {
                    *model_id = id_item->valueint;
                }
                if (brand_id_item && cJSON_IsNumber(brand_id_item)) {
                    *brand_id = brand_id_item->valueint;
                }

                ESP_LOGI(TAG, "Found device: %s %s (bid=%d, mid=%d)", 
                         brand, model, *brand_id, *model_id);
                cJSON_Delete(response_json);
                free(response);
                free(auth_body);
                return ESP_OK;
            }
        }

        cJSON_Delete(response_json);
        free(response);
        free(auth_body);
        if (array_size < page) {
            ESP_LOGW(TAG, "Device not found: %s %s", brand, model);
            return ESP_ERR_NOT_FOUND;
        }
        from += page;
    }
}

esp_err_t irext_api_clear_cache(void)
{
    ESP_LOGI(TAG, "API cache cleared");
    return ESP_OK;
}

esp_err_t irext_api_get_last_error(void)
{
    return g_last_error;
}

esp_err_t irext_api_get_brand_id(ir_device_category_t category, const char *brand_name, uint32_t *brand_id)
{
    if (!brand_name || !brand_id) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = irext_auth_refresh_if_needed();
    if (err != ESP_OK) return err;

    int from = 0;
    const int page = 50;
    *brand_id = 0;

    for (;;) {
        ESP_LOGD(TAG, "Brands page: category=%d from=%d count=%d target='%s'", category, from, page, brand_name);
        cJSON *json = cJSON_CreateObject();
        cJSON_AddNumberToObject(json, "categoryId", to_server_category_id(category));
        cJSON_AddNumberToObject(json, "from", from);
        cJSON_AddNumberToObject(json, "count", page);
        char *base_json = cJSON_Print(json);
        cJSON_Delete(json);

        char auth_body[2048];
        err = irext_auth_build_request_body(base_json, auth_body, sizeof(auth_body));
        free(base_json);
        if (err != ESP_OK) return err;

        char *response = NULL; size_t response_len = 0;
        err = http_post_request("/indexing/list_brands", auth_body, &response, &response_len);
        if (err != ESP_OK || !response) {
            if (response) free(response);
            return err != ESP_OK ? err : ESP_ERR_INVALID_RESPONSE;
        }

        bool has_more = false;
        cJSON *resp = cJSON_Parse(response);
        if (resp) {
            cJSON *status = cJSON_GetObjectItem(resp, "status");
            cJSON *code = status ? cJSON_GetObjectItem(status, "code") : NULL;
            if (code && cJSON_IsNumber(code) && code->valueint == 0) {
                cJSON *entity = cJSON_GetObjectItem(resp, "entity");
                if (cJSON_IsArray(entity)) {
                    int n = cJSON_GetArraySize(entity);
                    has_more = n >= page;
                    ESP_LOGD(TAG, "Brands parsed=%d (from=%d)", n, from);
                    int preview = n > 10 ? 10 : n;
                    for (int i = 0; i < preview; i++) {
                        cJSON *b = cJSON_GetArrayItem(entity, i);
                        cJSON *name = cJSON_GetObjectItem(b, "name");
                        cJSON *id = cJSON_GetObjectItem(b, "id");
                        if (name && id && cJSON_IsString(name) && cJSON_IsNumber(id)) {
                            ESP_LOGD(TAG, "Brand[%d]=%s id=%d", i, name->valuestring, id->valueint);
                        }
                    }
                    for (int i = 0; i < n; i++) {
                        cJSON *b = cJSON_GetArrayItem(entity, i);
                        cJSON *name = cJSON_GetObjectItem(b, "name");
                        cJSON *id = cJSON_GetObjectItem(b, "id");
                        if (name && id && cJSON_IsString(name) && cJSON_IsNumber(id)) {
                            if (strcasecmp(name->valuestring, brand_name) == 0) {
                                *brand_id = id->valueint;
                                ESP_LOGI(TAG, "Brand matched: '%s' -> id=%d", name->valuestring, *brand_id);
                                break;
                            }
                        }
                    }
                } else {
                    ESP_LOGE(TAG, "Brands: entity not array");
                }
            } else {
                cJSON *code_item = cJSON_GetObjectItem(status, "code");
                int error_code = code_item && cJSON_IsNumber(code_item) ? code_item->valueint : -1;
                ESP_LOGE(TAG, "Brands status code invalid: %d", error_code);
                
                switch(error_code) {
                    case 1:
                        ESP_LOGE(TAG, "Brand Query Error: Authentication failed - Check token validity");
                        break;
                    case 2:
                        ESP_LOGE(TAG, "Brand Query Error: Invalid category - Check device category");
                        break;
                    default:
                        ESP_LOGE(TAG, "Brand Query Error: Unknown error code %d", error_code);
                        break;
                }
            }
            cJSON_Delete(resp);
        } else {
            ESP_LOGE(TAG, "Brands JSON parse failed");
        }
        free(response);

        if (*brand_id) return ESP_OK;
        if (!has_more) break;
        from += page;
    }

    ESP_LOGE(TAG, "Brand '%s' not found after paging", brand_name);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t irext_api_decode_command(uint32_t index_id, uint32_t key_code, const ir_ac_status_t *ac_status, 
                                   uint32_t change_wind_dir, uint32_t para_data, 
                                   uint32_t **ir_data, size_t *data_length)
{
    if (!ir_data || !data_length) {
        return ESP_ERR_INVALID_ARG;
    }

    *ir_data = NULL;
    *data_length = 0;

    const int max_retries = 2;
    esp_err_t last_error = ESP_FAIL;
    
    for (int retry = 0; retry < max_retries; retry++) {
        esp_err_t err = irext_auth_refresh_if_needed();
        if (err != ESP_OK) return err;

        cJSON *json = cJSON_CreateObject();
        if (!json) {
            return ESP_ERR_NO_MEM;
        }

        cJSON_AddNumberToObject(json, "indexId", index_id);
        cJSON_AddNumberToObject(json, "keyCode", key_code);
        
        if (ac_status) {
            uint8_t temp_offset = ac_status->temperature;
            ir_ac_parameters_t p;
            if (irext_api_get_ac_parameters(index_id, ac_status->mode, &p) == ESP_OK) {
                if (temp_offset < p.temp_min) temp_offset = p.temp_min;
                if (temp_offset > p.temp_max) temp_offset = p.temp_max;
                if (p.temp_min <= p.temp_max) {
                    temp_offset = temp_offset - p.temp_min;
                } else {
                    temp_offset = temp_offset - 16;
                }
            } else {
                temp_offset = (temp_offset >= 16) ? (temp_offset - 16) : 0;
            }
            cJSON *ac_status_obj = cJSON_CreateObject();
            if (!ac_status_obj) {
                cJSON_Delete(json);
                return ESP_ERR_NO_MEM;
            }
            int acPower = (ac_status->power == IR_AC_POWER_ON) ? 0 : 1;
            cJSON_AddNumberToObject(ac_status_obj, "acPower", acPower);
            cJSON_AddNumberToObject(ac_status_obj, "acMode", ac_status->mode);
            cJSON_AddNumberToObject(ac_status_obj, "acTemp", temp_offset);
            cJSON_AddNumberToObject(ac_status_obj, "acWindSpeed", ac_status->wind_speed);
            int acWindDir = (ac_status->swing == IR_AC_SWING_ON) ? 1 : 0;
            cJSON_AddNumberToObject(ac_status_obj, "acWindDir", acWindDir);
            cJSON_AddItemToObject(json, "acStatus", ac_status_obj);
        }
        
        cJSON_AddNumberToObject(json, "changeWindDir", change_wind_dir);
        cJSON_AddNumberToObject(json, "paraData", para_data);

        char *base_json = cJSON_Print(json);
        cJSON_Delete(json);

        if (!base_json) {
            ESP_LOGE(TAG, "Failed to print JSON");
            return ESP_ERR_NO_MEM;
        }

        size_t auth_body_size = 4096;
        char *auth_body = (char *)heap_caps_malloc(auth_body_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!auth_body) auth_body = (char *)malloc(auth_body_size);
        if (!auth_body) {
            ESP_LOGE(TAG, "Failed to allocate auth body buffer");
            free(base_json);
            return ESP_ERR_NO_MEM;
        }

        err = irext_auth_build_request_body(base_json, auth_body, auth_body_size);
        free(base_json);

        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to build auth request body: %s", esp_err_to_name(err));
            free(auth_body);
            return err;
        }

        char *response = NULL;
        size_t response_len = 0;
        err = http_post_request("/operation/decode", auth_body, &response, &response_len);

        if (err != ESP_OK) {
            ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
            free(auth_body);
            last_error = err;
            continue;
        }

        if (!response) {
            ESP_LOGE(TAG, "Received null response");
            free(auth_body);
            last_error = ESP_ERR_INVALID_RESPONSE;
            continue;
        }

        cJSON *response_json = cJSON_Parse(response);
        if (!response_json) {
            ESP_LOGE(TAG, "Failed to parse response JSON");
            free(response);
            free(auth_body);
            last_error = ESP_ERR_INVALID_RESPONSE;
            continue;
        }

        cJSON *status_obj = cJSON_GetObjectItem(response_json, "status");
        if (!status_obj) {
            ESP_LOGE(TAG, "No status object in response");
            cJSON_Delete(response_json);
            free(response);
            free(auth_body);
            last_error = ESP_ERR_INVALID_RESPONSE;
            continue;
        }

        cJSON *code_item = cJSON_GetObjectItem(status_obj, "code");
        if (!code_item || !cJSON_IsNumber(code_item)) {
            ESP_LOGE(TAG, "Invalid status code in response");
            cJSON_Delete(response_json);
            free(response);
            free(auth_body);
            last_error = ESP_ERR_INVALID_RESPONSE;
            continue;
        }

        if (code_item->valueint != 0) {
            int error_code = code_item->valueint;
            ESP_LOGE(TAG, "Decode API failed with code %d (attempt %d/%d)", error_code, retry + 1, max_retries);
            
            bool should_retry = false;
            switch(error_code) {
                case 1:
                    ESP_LOGE(TAG, "Decode Error: Authentication failed - Token invalid or expired");
                    if (retry < max_retries - 1) {
                        ESP_LOGW(TAG, "Clearing auth cache and retrying...");
                        irext_auth_clear_cache();
                        should_retry = true;
                    }
                    break;
                case 2:
                    ESP_LOGE(TAG, "Decode Error: Invalid parameters - Check index_id, key_code or AC status");
                    break;
                case 3:
                    ESP_LOGE(TAG, "Decode Error: Device not found or decode failed");
                    break;
                case 4:
                    ESP_LOGE(TAG, "Decode Error: Protocol not supported");
                    break;
                case -1:
                default:
                    ESP_LOGE(TAG, "Decode Error: Unknown error code %d", error_code);
                    if (retry < max_retries - 1 && error_code == -1) {
                        ESP_LOGW(TAG, "Unknown error, clearing cache and retrying...");
                        irext_auth_clear_cache();
                        should_retry = true;
                    }
                    break;
            }
            
            cJSON_Delete(response_json);
            free(response);
            free(auth_body);
            last_error = ESP_FAIL;
            
            if (should_retry) {
                continue;
            } else {
                break;
            }
        }

        cJSON *entity = cJSON_GetObjectItem(response_json, "entity");
        if (!entity || !cJSON_IsArray(entity)) {
            cJSON_Delete(response_json);
            free(response);
            free(auth_body);
            last_error = ESP_ERR_INVALID_RESPONSE;
            continue;
        }

        int array_size = cJSON_GetArraySize(entity);
        if ((array_size % 2) != 0) {
            ESP_LOGW(TAG, "Decoded timings length is odd: %d", array_size);
        }
        if (array_size < 2) {
            ESP_LOGW(TAG, "Decoded timings length too short: %d", array_size);
        }

        *ir_data = (uint32_t *)heap_caps_malloc(array_size * sizeof(uint32_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!*ir_data) *ir_data = (uint32_t *)malloc(array_size * sizeof(uint32_t));
        if (!*ir_data) {
            cJSON_Delete(response_json);
            free(response);
            free(auth_body);
            return ESP_ERR_NO_MEM;
        }

        *data_length = array_size;
        
        for (int i = 0; i < array_size; i++) {
            cJSON *item = cJSON_GetArrayItem(entity, i);
            if (cJSON_IsNumber(item)) {
                (*ir_data)[i] = item->valueint;
            } else {
                (*ir_data)[i] = 0; 
            }
        }

        cJSON_Delete(response_json);
        free(response);
        free(auth_body);
        return ESP_OK;
    }
    
    return last_error;
}

esp_err_t irext_api_get_categories(ir_category_t *categories, size_t max_categories, size_t *found_count)
{
    if (!categories || !found_count) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = irext_auth_refresh_if_needed();
    if (err != ESP_OK) return err;

    size_t total = 0;
    int from = 0;
    const int page = 20;

    while (total < max_categories) {
        cJSON *json = cJSON_CreateObject();
        cJSON_AddNumberToObject(json, "from", from);
        cJSON_AddNumberToObject(json, "count", page);

        char *base_json = cJSON_Print(json);
        cJSON_Delete(json);

        char auth_body[2048];
        err = irext_auth_build_request_body(base_json, auth_body, sizeof(auth_body));
        free(base_json);

        if (err != ESP_OK) return err;

        char *response;
        size_t response_len;
        err = http_post_request("/indexing/list_categories", auth_body, &response, &response_len);

        if (err != ESP_OK) {
            free(response);
            break;
        }

        size_t added = 0;
        cJSON *response_json = cJSON_Parse(response);
        if (response_json) {
            cJSON *status_obj = cJSON_GetObjectItem(response_json, "status");
            if (status_obj) {
                cJSON *code_item = cJSON_GetObjectItem(status_obj, "code");
                if (code_item && cJSON_IsNumber(code_item) && code_item->valueint == 0) {
                    cJSON *entity = cJSON_GetObjectItem(response_json, "entity");
                    if (cJSON_IsArray(entity)) {
                        int array_size = cJSON_GetArraySize(entity);
                        int take = array_size;
                        for (int i = 0; i < take && total < max_categories; i++) {
                            cJSON *cat_item = cJSON_GetArrayItem(entity, i);
                            if (cat_item) {
                                cJSON *id_item = cJSON_GetObjectItem(cat_item, "id");
                                cJSON *name_item = cJSON_GetObjectItem(cat_item, "name");
                                cJSON *status_item = cJSON_GetObjectItem(cat_item, "status");

                                if (id_item && name_item && cJSON_IsNumber(id_item) && cJSON_IsString(name_item)) {
                                    categories[total].id = id_item->valueint;
                                    strncpy(categories[total].name, name_item->valuestring, IR_MAX_BRAND_NAME_LEN - 1);
                                    categories[total].name[IR_MAX_BRAND_NAME_LEN - 1] = '\0';
                                    categories[total].status = status_item ? status_item->valueint : 1;
                                    total++;
                                    added++;
                                }
                            }
                        }
                        if (array_size < page) {
                            cJSON_Delete(response_json);
                            free(response);
                            break;
                        }
                    }
                } else {
                    err = ESP_FAIL;
                }
            } else {
                err = ESP_ERR_INVALID_RESPONSE;
            }
            cJSON_Delete(response_json);
        } else {
            err = ESP_ERR_INVALID_RESPONSE;
        }
        free(response);
        if (added == 0) break;
        from += page;
    }

    *found_count = total;
    return err;
}

esp_err_t irext_api_get_ac_parameters(uint32_t index_id, uint8_t mode, ir_ac_parameters_t *parameters)
{
    if (!parameters) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = irext_auth_refresh_if_needed();
    if (err != ESP_OK) return err;

    cJSON *json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "indexId", index_id);
    cJSON_AddNumberToObject(json, "mode", mode);

    char *base_json = cJSON_Print(json);
    cJSON_Delete(json);

    char auth_body[2048];
    err = irext_auth_build_request_body(base_json, auth_body, sizeof(auth_body));
    free(base_json);

    if (err != ESP_OK) return err;

    char *response;
    size_t response_len;
    err = http_post_request("/operation/get_ac_parameters", auth_body, &response, &response_len);

    if (err == ESP_OK) {
        cJSON *response_json = cJSON_Parse(response);
        if (response_json) {
            cJSON *status_obj = cJSON_GetObjectItem(response_json, "status");
            if (status_obj) {
                cJSON *code_item = cJSON_GetObjectItem(status_obj, "code");
                if (code_item && cJSON_IsNumber(code_item) && code_item->valueint == 0) {
                    cJSON *entity = cJSON_GetObjectItem(response_json, "entity");
                    if (entity) {
                        cJSON *temp_min = cJSON_GetObjectItem(entity, "tempMin");
                        cJSON *temp_max = cJSON_GetObjectItem(entity, "tempMax");
                        cJSON *modes = cJSON_GetObjectItem(entity, "supportedModes");
                        if (!modes) modes = cJSON_GetObjectItem(entity, "modes");
                        cJSON *wind_speed = cJSON_GetObjectItem(entity, "supportedWindSpeed");
                        if (!wind_speed) wind_speed = cJSON_GetObjectItem(entity, "windSpeed");
                        cJSON *swing = cJSON_GetObjectItem(entity, "supportedSwing");
                        if (!swing) swing = cJSON_GetObjectItem(entity, "swing");
                        cJSON *wind_dir = cJSON_GetObjectItem(entity, "supportedWindDirections");
                        if (!wind_dir) wind_dir = cJSON_GetObjectItem(entity, "windDirections");

                        if (temp_min && cJSON_IsNumber(temp_min)) {
                            parameters->temp_min = temp_min->valueint;
                        }
                        if (temp_max && cJSON_IsNumber(temp_max)) {
                            parameters->temp_max = temp_max->valueint;
                        }
                        
                        if (modes && cJSON_IsArray(modes)) {
                            int mode_count = cJSON_GetArraySize(modes);
                            for (int i = 0; i < 5 && i < mode_count; i++) {
                                cJSON *mode_item = cJSON_GetArrayItem(modes, i);
                                if (cJSON_IsNumber(mode_item)) {
                                    parameters->supported_modes[i] = mode_item->valueint;
                                }
                            }
                        }
                        
                        if (wind_speed && cJSON_IsArray(wind_speed)) {
                            int speed_count = cJSON_GetArraySize(wind_speed);
                            for (int i = 0; i < 4 && i < speed_count; i++) {
                                cJSON *speed_item = cJSON_GetArrayItem(wind_speed, i);
                                if (cJSON_IsNumber(speed_item)) {
                                    parameters->supported_wind_speed[i] = speed_item->valueint;
                                }
                            }
                        }
                        
                        if (swing && cJSON_IsArray(swing)) {
                            int swing_count = cJSON_GetArraySize(swing);
                            for (int i = 0; i < 2 && i < swing_count; i++) {
                                cJSON *swing_item = cJSON_GetArrayItem(swing, i);
                                if (cJSON_IsNumber(swing_item)) {
                                    parameters->supported_swing[i] = swing_item->valueint;
                                }
                            }
                        }
                        
                        if (wind_dir && cJSON_IsNumber(wind_dir)) {
                            parameters->supported_wind_directions = wind_dir->valueint;
                        }
                        
                        ESP_LOGD(TAG, "Retrieved AC parameters for index %d", index_id);
                    }
                } else {
                    int error_code = code_item ? code_item->valueint : -1;
                    ESP_LOGE(TAG, "AC parameters API failed with code %d", error_code);
                    
                    switch(error_code) {
                        case 1:
                            ESP_LOGE(TAG, "AC Parameters Error: Authentication failed - Token invalid or expired");
                            break;
                        case 2:
                            ESP_LOGE(TAG, "AC Parameters Error: Invalid parameters - Check index_id and mode");
                            break;
                        case 3:
                            ESP_LOGE(TAG, "AC Parameters Error: Device not found or not supported");
                            break;
                        default:
                            ESP_LOGE(TAG, "AC Parameters Error: Unknown error code %d", error_code);
                            break;
                    }
                    err = ESP_FAIL;
                }
            } else {
                err = ESP_ERR_INVALID_RESPONSE;
            }
            cJSON_Delete(response_json);
        } else {
            err = ESP_ERR_INVALID_RESPONSE;
        }
    }

    free(response);
    return err;
} 

esp_err_t irext_api_list_provinces(ir_province_t *provinces, size_t max_count, size_t *found_count)
{
    if (!provinces || !found_count) return ESP_ERR_INVALID_ARG;
    esp_err_t err = irext_auth_refresh_if_needed();
    if (err != ESP_OK) return err;

    cJSON *json = cJSON_CreateObject();
    char *base_json = cJSON_Print(json);
    cJSON_Delete(json);

    char auth_body[256];
    err = irext_auth_build_request_body(base_json, auth_body, sizeof(auth_body));
    free(base_json);
    if (err != ESP_OK) return err;

    char *response = NULL; size_t response_len = 0;
    err = http_post_request("/indexing/list_provinces", auth_body, &response, &response_len);
    if (err != ESP_OK || !response) {
        if (response) free(response);
        return err != ESP_OK ? err : ESP_ERR_INVALID_RESPONSE;
    }

    size_t total = 0;
    cJSON *resp = cJSON_Parse(response);
    if (resp) {
        cJSON *status = cJSON_GetObjectItem(resp, "status");
        cJSON *code = status ? cJSON_GetObjectItem(status, "code") : NULL;
        if (code && cJSON_IsNumber(code) && code->valueint == 0) {
            cJSON *entity = cJSON_GetObjectItem(resp, "entity");
            if (cJSON_IsArray(entity)) {
                int n = cJSON_GetArraySize(entity);
                for (int i = 0; i < n && total < max_count; i++) {
                    cJSON *item = cJSON_GetArrayItem(entity, i);
                    cJSON *code_item = cJSON_GetObjectItem(item, "code");
                    cJSON *name_item = cJSON_GetObjectItem(item, "name");
                    if (code_item && name_item && cJSON_IsString(code_item) && cJSON_IsString(name_item)) {
                        strncpy(provinces[total].code, code_item->valuestring, sizeof(provinces[total].code) - 1);
                        provinces[total].code[sizeof(provinces[total].code) - 1] = '\0';
                        strncpy(provinces[total].name, name_item->valuestring, sizeof(provinces[total].name) - 1);
                        provinces[total].name[sizeof(provinces[total].name) - 1] = '\0';
                        total++;
                    }
                }
            } else {
                err = ESP_ERR_INVALID_RESPONSE;
            }
        } else {
            err = ESP_FAIL;
        }
        cJSON_Delete(resp);
    } else {
        err = ESP_ERR_INVALID_RESPONSE;
    }

    free(response);
    *found_count = total;
    return err;
}

esp_err_t irext_api_list_cities(const char *province_prefix, ir_city_t *cities, size_t max_count, size_t *found_count)
{
    if (!province_prefix || !cities || !found_count) return ESP_ERR_INVALID_ARG;
    esp_err_t err = irext_auth_refresh_if_needed();
    if (err != ESP_OK) return err;

    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "provincePrefix", province_prefix);
    char *base_json = cJSON_Print(json);
    cJSON_Delete(json);

    char auth_body[256];
    err = irext_auth_build_request_body(base_json, auth_body, sizeof(auth_body));
    free(base_json);
    if (err != ESP_OK) return err;

    char *response = NULL; size_t response_len = 0;
    err = http_post_request("/indexing/list_cities", auth_body, &response, &response_len);
    if (err != ESP_OK || !response) {
        if (response) free(response);
        return err != ESP_OK ? err : ESP_ERR_INVALID_RESPONSE;
    }

    size_t total = 0;
    cJSON *resp = cJSON_Parse(response);
    if (resp) {
        cJSON *status = cJSON_GetObjectItem(resp, "status");
        cJSON *code = status ? cJSON_GetObjectItem(status, "code") : NULL;
        if (code && cJSON_IsNumber(code) && code->valueint == 0) {
            cJSON *entity = cJSON_GetObjectItem(resp, "entity");
            if (cJSON_IsArray(entity)) {
                int n = cJSON_GetArraySize(entity);
                for (int i = 0; i < n && total < max_count; i++) {
                    cJSON *item = cJSON_GetArrayItem(entity, i);
                    cJSON *city_code = cJSON_GetObjectItem(item, "code");
                    cJSON *name_item = cJSON_GetObjectItem(item, "name");
                    if (city_code && name_item && cJSON_IsString(city_code) && cJSON_IsString(name_item)) {
                        strncpy(cities[total].code, city_code->valuestring, sizeof(cities[total].code) - 1);
                        cities[total].code[sizeof(cities[total].code) - 1] = '\0';
                        strncpy(cities[total].name, name_item->valuestring, sizeof(cities[total].name) - 1);
                        cities[total].name[sizeof(cities[total].name) - 1] = '\0';
                        total++;
                    }
                }
            } else {
                err = ESP_ERR_INVALID_RESPONSE;
            }
        } else {
            err = ESP_FAIL;
        }
        cJSON_Delete(resp);
    } else {
        err = ESP_ERR_INVALID_RESPONSE;
    }

    free(response);
    *found_count = total;
    return err;
}

esp_err_t irext_api_list_operators(const char *city_code, ir_operator_t *operators, size_t max_count, size_t *found_count)
{
    if (!city_code || !operators || !found_count) return ESP_ERR_INVALID_ARG;
    esp_err_t err = irext_auth_refresh_if_needed();
    if (err != ESP_OK) return err;

    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "cityCode", city_code);
    cJSON_AddNumberToObject(json, "from", 0);
    cJSON_AddNumberToObject(json, "count", 100);
    char *base_json = cJSON_Print(json);
    cJSON_Delete(json);

    char auth_body[512];
    err = irext_auth_build_request_body(base_json, auth_body, sizeof(auth_body));
    free(base_json);
    if (err != ESP_OK) return err;

    char *response = NULL; size_t response_len = 0;
    err = http_post_request("/indexing/list_operators", auth_body, &response, &response_len);
    if (err != ESP_OK || !response) {
        if (response) free(response);
        return err != ESP_OK ? err : ESP_ERR_INVALID_RESPONSE;
    }

    size_t total = 0;
    cJSON *resp = cJSON_Parse(response);
    if (resp) {
        cJSON *status = cJSON_GetObjectItem(resp, "status");
        cJSON *code = status ? cJSON_GetObjectItem(status, "code") : NULL;
        if (code && cJSON_IsNumber(code) && code->valueint == 0) {
            cJSON *entity = cJSON_GetObjectItem(resp, "entity");
            if (cJSON_IsArray(entity)) {
                int n = cJSON_GetArraySize(entity);
                for (int i = 0; i < n && total < max_count; i++) {
                    cJSON *item = cJSON_GetArrayItem(entity, i);
                    cJSON *id_item = cJSON_GetObjectItem(item, "id");
                    cJSON *name_item = cJSON_GetObjectItem(item, "name");
                    if (id_item && name_item && cJSON_IsNumber(id_item) && cJSON_IsString(name_item)) {
                        operators[total].id = id_item->valueint;
                        strncpy(operators[total].name, name_item->valuestring, sizeof(operators[total].name) - 1);
                        operators[total].name[sizeof(operators[total].name) - 1] = '\0';
                        total++;
                    }
                }
            } else {
                err = ESP_ERR_INVALID_RESPONSE;
            }
        } else {
            err = ESP_FAIL;
        }
        cJSON_Delete(resp);
    } else {
        err = ESP_ERR_INVALID_RESPONSE;
    }

    free(response);
    *found_count = total;
    return err;
} 

esp_err_t irext_api_list_indexes_by_city(const char *city_code, uint32_t *index_ids, size_t max_count, size_t *found_count)
{
    if (!city_code || !index_ids || !found_count) return ESP_ERR_INVALID_ARG;
    esp_err_t err = irext_auth_refresh_if_needed();
    if (err != ESP_OK) return err;

    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "cityCode", city_code);
    cJSON_AddNumberToObject(json, "from", 0);
    cJSON_AddNumberToObject(json, "count", 50);
    char *base_json = cJSON_Print(json);
    cJSON_Delete(json);

    char auth_body[512];
    err = irext_auth_build_request_body(base_json, auth_body, sizeof(auth_body));
    free(base_json);
    if (err != ESP_OK) return err;

    char *response = NULL; size_t response_len = 0;
    err = http_post_request("/indexing/list_indexes", auth_body, &response, &response_len);
    if (err != ESP_OK || !response) {
        if (response) free(response);
        return err != ESP_OK ? err : ESP_ERR_INVALID_RESPONSE;
    }

    size_t total = 0;
    cJSON *resp = cJSON_Parse(response);
    if (resp) {
        cJSON *status = cJSON_GetObjectItem(resp, "status");
        cJSON *code = status ? cJSON_GetObjectItem(status, "code") : NULL;
        if (code && cJSON_IsNumber(code) && code->valueint == 0) {
            cJSON *entity = cJSON_GetObjectItem(resp, "entity");
            if (cJSON_IsArray(entity)) {
                int n = cJSON_GetArraySize(entity);
                for (int i = 0; i < n && total < max_count; i++) {
                    cJSON *item = cJSON_GetArrayItem(entity, i);
                    cJSON *id_item = cJSON_GetObjectItem(item, "id");
                    if (id_item && cJSON_IsNumber(id_item)) {
                        index_ids[total++] = id_item->valueint;
                    }
                }
            } else {
                err = ESP_ERR_INVALID_RESPONSE;
            }
        } else {
            err = ESP_FAIL;
        }
        cJSON_Delete(resp);
    } else {
        err = ESP_ERR_INVALID_RESPONSE;
    }

    free(response);
    *found_count = total;
    return err;
}

// Dynamic mapping from local enum to server categoryId
static uint32_t s_server_category_map[32] = {0};
static bool s_category_map_ready = false;

static bool str_contains_icase(const char *haystack, const char *needle)
{
    if (!haystack || !needle) return false;
    return strcasestr(haystack, needle) != NULL;
}

static void ensure_category_mapping_loaded(void)
{
    if (s_category_map_ready) return;
    ir_category_t cats[32]; size_t n = 0;
    if (irext_api_get_categories(cats, 32, &n) == ESP_OK) {
        for (size_t i = 0; i < n; i++) {
            const char *name = cats[i].name;
            if (!s_server_category_map[IR_DEVICE_AC] && (str_contains_icase(name, "空调") || str_contains_icase(name, "air"))) s_server_category_map[IR_DEVICE_AC] = cats[i].id;
            else if (!s_server_category_map[IR_DEVICE_TV] && (str_contains_icase(name, "电视") || str_contains_icase(name, "tv"))) s_server_category_map[IR_DEVICE_TV] = cats[i].id;
            else if (!s_server_category_map[IR_DEVICE_STB] && (str_contains_icase(name, "机顶盒") || str_contains_icase(name, "stb"))) s_server_category_map[IR_DEVICE_STB] = cats[i].id;
            else if (!s_server_category_map[IR_DEVICE_DVD] && str_contains_icase(name, "dvd")) s_server_category_map[IR_DEVICE_DVD] = cats[i].id;
            else if (!s_server_category_map[IR_DEVICE_FAN] && (str_contains_icase(name, "风扇") || str_contains_icase(name, "fan"))) s_server_category_map[IR_DEVICE_FAN] = cats[i].id;
            else if (!s_server_category_map[IR_DEVICE_LIGHT] && (str_contains_icase(name, "灯") || str_contains_icase(name, "light"))) s_server_category_map[IR_DEVICE_LIGHT] = cats[i].id;
            else if (!s_server_category_map[IR_DEVICE_PROJECTOR] && (str_contains_icase(name, "投影") || str_contains_icase(name, "projector"))) s_server_category_map[IR_DEVICE_PROJECTOR] = cats[i].id;
            else if (!s_server_category_map[IR_DEVICE_STEREO] && (str_contains_icase(name, "音响") || str_contains_icase(name, "stereo") || str_contains_icase(name, "audio"))) s_server_category_map[IR_DEVICE_STEREO] = cats[i].id;
            else if (!s_server_category_map[IR_DEVICE_AIR_PURIFIER] && (str_contains_icase(name, "空气净化") || str_contains_icase(name, "空净") || str_contains_icase(name, "purifier"))) s_server_category_map[IR_DEVICE_AIR_PURIFIER] = cats[i].id;
            else if (!s_server_category_map[IR_DEVICE_ROBOT_VACUUM] && (str_contains_icase(name, "扫地机") || str_contains_icase(name, "机器人") || str_contains_icase(name, "robot"))) s_server_category_map[IR_DEVICE_ROBOT_VACUUM] = cats[i].id;
            else if (!s_server_category_map[IR_DEVICE_BOX] && str_contains_icase(name, "盒子")) s_server_category_map[IR_DEVICE_BOX] = cats[i].id;
            else if (!s_server_category_map[IR_DEVICE_IPTV] && str_contains_icase(name, "iptv")) s_server_category_map[IR_DEVICE_IPTV] = cats[i].id;
            else if (!s_server_category_map[IR_DEVICE_BULB] && (str_contains_icase(name, "灯泡") || str_contains_icase(name, "bulb"))) s_server_category_map[IR_DEVICE_BULB] = cats[i].id;
            else if (!s_server_category_map[IR_DEVICE_DYSON] && str_contains_icase(name, "dyson")) s_server_category_map[IR_DEVICE_DYSON] = cats[i].id;
            else if (!s_server_category_map[IR_DEVICE_CAMERA] && (str_contains_icase(name, "相机") || str_contains_icase(name, "camera"))) s_server_category_map[IR_DEVICE_CAMERA] = cats[i].id;
            else if (!s_server_category_map[IR_DEVICE_HEATER] && (str_contains_icase(name, "取暖") || str_contains_icase(name, "暖风") || str_contains_icase(name, "heater"))) s_server_category_map[IR_DEVICE_HEATER] = cats[i].id;
        }
        if (!s_server_category_map[IR_DEVICE_BULB] && s_server_category_map[IR_DEVICE_LIGHT]) {
            s_server_category_map[IR_DEVICE_BULB] = s_server_category_map[IR_DEVICE_LIGHT];
        }
    }
    s_category_map_ready = true;
}

static uint32_t to_server_category_id(ir_device_category_t category)
{
    ensure_category_mapping_loaded();
    uint32_t mapped = 0;
    if ((int)category >= 0 && (int)category < (int)(sizeof(s_server_category_map)/sizeof(s_server_category_map[0]))) {
        mapped = s_server_category_map[category];
    }
    return mapped ? mapped : (uint32_t)category;
} 

