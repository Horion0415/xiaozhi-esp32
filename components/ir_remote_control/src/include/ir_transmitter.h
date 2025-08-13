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
 * @brief Initialize IR transmitter hardware (LEDC + GPTimer backend)
 *
 * @param config IR transmitter configuration (GPIO, carrier frequency)
 * @return ESP_OK on success
 */
esp_err_t ir_transmitter_init(const ir_tx_config_t *config);

/**
 * @brief Deinitialize IR transmitter hardware
 *
 * @return ESP_OK on success
 */
esp_err_t ir_transmitter_deinit(void);

/**
 * @brief Transmit timing pairs in microseconds (on, off, on, off, ...)
 *
 * @param timing_data Array of timing pairs in microseconds
 * @param length Length of the array (must be even; odd length will be trimmed)
 * @return ESP_OK on success
 */
esp_err_t ir_transmitter_send_timing_data(const uint32_t *timing_data, size_t length);

/**
 * @brief Check if IR transmitter is initialized
 */
bool ir_transmitter_is_ready(void);

/**
 * @brief Get IR transmitter statistics
 */
esp_err_t ir_transmitter_get_status(uint32_t *tx_count, uint32_t *error_count);

#ifdef __cplusplus
}
#endif 