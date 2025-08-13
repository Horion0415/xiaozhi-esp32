/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "ir_remote_control.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief IRext Web API endpoints following official specification
 */
#define IREXT_API_LOGIN             "/app/app_login"
#define IREXT_API_LIST_CATEGORIES   "/indexing/list_categories"
#define IREXT_API_LIST_BRANDS       "/indexing/list_brands"
#define IREXT_API_LIST_INDEXES      "/indexing/list_indexes"
#define IREXT_API_DOWNLOAD_BIN      "/operation/download_bin"
#define IREXT_API_DECODE            "/operation/decode"
#define IREXT_API_GET_AC_PARAMS     "/operation/get_ac_parameters"

/**
 * @brief IRext API response structure
 */
typedef struct {
    int status_code;                ///< HTTP status code
    char *data;                     ///< Response data
    size_t data_length;             ///< Data length
    esp_err_t error;                ///< Error code
} irext_api_response_t;

/**
 * @brief Initialize IRext API client
 * 
 * @param config IRext configuration
 * @return ESP_OK on success
 */
esp_err_t irext_api_init(const ir_irext_config_t *config);

/**
 * @brief Deinitialize IRext API client
 * 
 * @return ESP_OK on success
 */
esp_err_t irext_api_deinit(void);

/**
 * @brief Send AC command via IRext online decoding
 * 
 * Makes authenticated API request to decode AC protocol and transmit IR signal
 * 
 * @param device_info Device information (brand, model, etc.)
 * @param ac_status AC status to apply
 * @return ESP_OK on success
 */
esp_err_t irext_api_send_ac_command(const ir_device_info_t *device_info, 
                                    const ir_ac_status_t *ac_status);

// TV commands are sent via generic key API

/**
 * @brief Send generic key command via IRext online decoding
 * 
 * @param device_info Device information
 * @param key_code Key code to send
 * @return ESP_OK on success
 */
esp_err_t irext_api_send_key_command(const ir_device_info_t *device_info, 
                                     uint32_t key_code);

/**
 * @brief Get supported brands for a device category
 * 
 * Calls IRext /indexing/list_brands API with authentication
 * 
 * @param category Device category
 * @param brands Array to store brand names
 * @param max_brands Maximum number of brands to retrieve
 * @param found_count Number of brands actually found
 * @return ESP_OK on success
 */
esp_err_t irext_api_get_brands(ir_device_category_t category, 
                               char brands[][IR_MAX_BRAND_NAME_LEN],
                               size_t max_brands, 
                               size_t *found_count);

/**
 * @brief Get supported models for a brand
 * 
 * @param category Device category
 * @param brand Brand name
 * @param models Array to store model names
 * @param max_models Maximum number of models to retrieve
 * @param found_count Number of models actually found
 * @return ESP_OK on success
 */
esp_err_t irext_api_get_models(ir_device_category_t category,
                               const char *brand,
                               char models[][IR_MAX_MODEL_NAME_LEN],
                               size_t max_models, 
                               size_t *found_count);

/**
 * @brief Search brands by pattern
 * 
 * @param category Device category
 * @param pattern Search pattern
 * @param brands Array to store matching brand names
 * @param max_brands Maximum number of brands
 * @param found_count Number of matching brands found
 * @return ESP_OK on success
 */
esp_err_t irext_api_search_brands(ir_device_category_t category,
                                  const char *pattern,
                                  char brands[][IR_MAX_BRAND_NAME_LEN],
                                  size_t max_brands, 
                                  size_t *found_count);

/**
 * @brief Search models by pattern
 * 
 * @param category Device category
 * @param brand Brand name
 * @param pattern Search pattern
 * @param models Array to store matching model names
 * @param max_models Maximum number of models
 * @param found_count Number of matching models found
 * @return ESP_OK on success
 */
esp_err_t irext_api_search_models(ir_device_category_t category,
                                  const char *brand,
                                  const char *pattern,
                                  char models[][IR_MAX_MODEL_NAME_LEN],
                                  size_t max_models, 
                                  size_t *found_count);

/**
 * @brief Find device by exact brand and model names
 * 
 * @param category Device category
 * @param brand Brand name
 * @param model Model name
 * @param brand_id Output: Brand ID from IRext
 * @param model_id Output: Model ID from IRext
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if device not found
 */
esp_err_t irext_api_find_device(ir_device_category_t category,
                                const char *brand,
                                const char *model,
                                uint32_t *brand_id,
                                uint32_t *model_id);

/**
 * @brief Clear API response cache
 * 
 * @return ESP_OK on success
 */
esp_err_t irext_api_clear_cache(void);

/**
 * @brief Get last API error
 * 
 * @return Last error code from API operations
 */
esp_err_t irext_api_get_last_error(void);

esp_err_t irext_api_get_categories(ir_category_t *categories, size_t max_categories, size_t *found_count);

esp_err_t irext_api_get_ac_parameters(uint32_t index_id, uint8_t mode, ir_ac_parameters_t *parameters);

esp_err_t irext_api_decode_command(uint32_t index_id, uint32_t key_code, const ir_ac_status_t *ac_status, 
                                   uint32_t change_wind_dir, uint32_t para_data, 
                                   uint32_t **ir_data, size_t *data_length);

typedef struct {
    uint32_t id;
    char name[IR_MAX_BRAND_NAME_LEN];
    uint32_t category_id;
    char category_name[IR_MAX_BRAND_NAME_LEN];
} ir_brand_t;

typedef struct {
    uint32_t id;
    uint32_t category_id;
    uint32_t brand_id;
    char protocol[64];
    char remote[64];
} ir_remote_index_t;

esp_err_t irext_api_get_brand_id(ir_device_category_t category, const char *brand_name, uint32_t *brand_id);

/** City/Province/Operator indexing structures */
typedef struct {
    char code[8];
    char name[32];
} ir_province_t;

typedef struct {
    char code[16];
    char name[32];
} ir_city_t;

typedef struct {
    uint32_t id;
    char name[64];
} ir_operator_t;

/** Indexing: list provinces */
esp_err_t irext_api_list_provinces(ir_province_t *provinces, size_t max_count, size_t *found_count);

/** Indexing: list cities by province prefix (e.g. "32") */
esp_err_t irext_api_list_cities(const char *province_prefix, ir_city_t *cities, size_t max_count, size_t *found_count);

/** Indexing: list operators by city code (e.g. "320100") */
esp_err_t irext_api_list_operators(const char *city_code, ir_operator_t *operators, size_t max_count, size_t *found_count);

/** Indexing: list remote index ids by city code (STB indexing path) */
esp_err_t irext_api_list_indexes_by_city(const char *city_code, uint32_t *index_ids, size_t max_count, size_t *found_count);

#ifdef __cplusplus
}
#endif 