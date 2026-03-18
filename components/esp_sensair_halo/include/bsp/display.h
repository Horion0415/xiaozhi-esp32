/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief ESP-SensairHalo BSP: Display API (no LVGL dependency)
 */

#pragma once

#include "esp_err.h"
#include "esp_lcd_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BSP_LCD_H_RES            284
#define BSP_LCD_V_RES            240
#define BSP_LCD_BITS_PER_PIXEL   16

typedef struct {
    int max_transfer_sz;
} bsp_display_config_t;

typedef struct {
    size_t max_transfer_bytes;
    uint32_t dma_burst_size;
    int trans_queue_depth;
} bsp_display_parlio_config_t;

/**
 * @brief Create LCD panel using the bus interface selected via Kconfig (BSP_LCD_BUS_TYPE).
 *
 * Exactly one of @p spi_config or @p parlio_config is used, depending on the
 * Kconfig selection.  Pass NULL for whichever config is not applicable; the
 * corresponding driver will then use its built-in defaults.
 *
 * @param[in]  spi_config    SPI display configuration (NULL for defaults, used when CONFIG_BSP_LCD_SPI)
 * @param[in]  parlio_config PARLIO display configuration (NULL for defaults, used when CONFIG_BSP_LCD_PARLIO)
 * @param[out] ret_panel     Panel handle
 * @param[out] ret_io        Panel IO handle
 * @return ESP_OK on success
 */
esp_err_t bsp_display_new(const bsp_display_config_t *spi_config,
                          const bsp_display_parlio_config_t *parlio_config,
                          esp_lcd_panel_handle_t *ret_panel,
                          esp_lcd_panel_io_handle_t *ret_io);

/**
 * @brief Set display brightness
 *
 * @param brightness_percent Brightness 0-100%
 * @return ESP_OK on success
 */
esp_err_t bsp_display_brightness_set(int brightness_percent);

esp_err_t bsp_display_backlight_on(void);
esp_err_t bsp_display_backlight_off(void);

#ifdef __cplusplus
}
#endif
