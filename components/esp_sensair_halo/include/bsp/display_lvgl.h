/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief ESP-SensairHalo BSP: Display + LVGL adapter helper API
 */

#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch.h"
#include "esp_lv_adapter.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief BSP-managed LVGL display context
 */
typedef struct {
    lv_display_t *disp;                      /*!< LVGL display handle */
    lv_indev_t *touch;                       /*!< LVGL touch input device */
    esp_lcd_panel_handle_t panel;            /*!< LCD panel handle */
    esp_lcd_panel_io_handle_t panel_io;      /*!< LCD panel IO handle */
    esp_lcd_touch_handle_t touch_handle;     /*!< Touch controller handle */
    esp_lv_adapter_rotation_t rotation;      /*!< Rotation passed to LVGL adapter */
} bsp_display_lvgl_ctx_t;

/**
 * @brief Initialize LCD panel, LVGL adapter, and touch input in one call
 *
 * This helper uses the BSP's default display initialization path and registers
 * the panel with esp_lvgl_adapter using the default PSRAM display profile.
 * Touch is created via bsp_touch_new() and registered to the same LVGL display.
 *
 * @param[out] ctx Context filled on success
 * @return ESP_OK on success
 */
esp_err_t bsp_display_lvgl_init(bsp_display_lvgl_ctx_t *ctx);

#ifdef __cplusplus
}
#endif
