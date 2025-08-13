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
 * @brief IRext authentication credentials structure
 * 
 * Following IRext Web API specification:
 * 1. Use APP Key and APP Secret to call login interface
 * 2. Get user id and token from response
 * 3. Include id and token in all subsequent API request bodies
 */
typedef struct {
    uint16_t user_id;               ///< User ID obtained from login
    char token[128];                ///< Authentication token
    uint64_t expires_at;            ///< Token expiration timestamp
    bool is_valid;                  ///< Whether credentials are valid
} irext_credentials_t;

/**
 * @brief Initialize IRext authentication system
 * 
 * @param config IRext configuration containing APP Key and Secret
 * @return ESP_OK on success
 */
esp_err_t irext_auth_init(const ir_irext_config_t *config);

/**
 * @brief Deinitialize IRext authentication system
 * 
 * @return ESP_OK on success
 */
esp_err_t irext_auth_deinit(void);

/**
 * @brief Perform login to IRext service following official API spec
 * 
 * Makes POST request to /app/app_login with APP Key and Secret
 * to obtain user ID and authentication token
 * 
 * @return ESP_OK on success
 */
esp_err_t irext_auth_login(void);

/**
 * @brief Get current authentication credentials
 * 
 * @param credentials Pointer to store credentials
 * @return ESP_OK if credentials are valid
 */
esp_err_t irext_auth_get_credentials(irext_credentials_t *credentials);

/**
 * @brief Check if authentication is valid and not expired
 * 
 * @return true if authenticated and token not expired
 */
bool irext_auth_is_valid(void);

/**
 * @brief Refresh authentication token if needed
 * 
 * @return ESP_OK on success
 */
esp_err_t irext_auth_refresh_if_needed(void);

/**
 * @brief Clear cached authentication credentials
 * 
 * @return ESP_OK on success
 */
esp_err_t irext_auth_clear_cache(void);

/**
 * @brief Build authenticated request body for IRext API calls
 * 
 * Adds required 'id' and 'token' parameters to request body
 * as specified by IRext Web API documentation
 * 
 * @param base_json Base JSON request body
 * @param output_buffer Buffer to store final JSON
 * @param buffer_size Size of output buffer
 * @return ESP_OK on success
 */
esp_err_t irext_auth_build_request_body(const char *base_json, 
                                        char *output_buffer, 
                                        size_t buffer_size);

#ifdef __cplusplus
}
#endif 