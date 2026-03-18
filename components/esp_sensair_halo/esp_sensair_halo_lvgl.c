/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <string.h>
#include "bsp/display_lvgl.h"
#include "bsp/display.h"
#include "bsp/esp_sensair_halo.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"

static const char *TAG = "BSP_SENSAIR_LVGL";
static const size_t BSP_LCD_FULL_FRAME_BYTES = BSP_LCD_H_RES * BSP_LCD_V_RES * (BSP_LCD_BITS_PER_PIXEL / 8);

static void bsp_display_lvgl_cleanup(bsp_display_lvgl_ctx_t *ctx, bool adapter_initialized)
{
    if (ctx == NULL) {
        return;
    }

    if (adapter_initialized && ctx->touch != NULL) {
        esp_lv_adapter_unregister_touch(ctx->touch);
        ctx->touch = NULL;
    }

    if (ctx->touch_handle != NULL) {
        esp_lcd_touch_del(ctx->touch_handle);
        ctx->touch_handle = NULL;
    }

    if (adapter_initialized && ctx->disp != NULL) {
        esp_lv_adapter_unregister_display(ctx->disp);
        ctx->disp = NULL;
    }

    if (adapter_initialized) {
        esp_lv_adapter_deinit();
    }

    if (ctx->panel != NULL) {
        esp_lcd_panel_del(ctx->panel);
        ctx->panel = NULL;
    }

    if (ctx->panel_io != NULL) {
        esp_lcd_panel_io_del(ctx->panel_io);
        ctx->panel_io = NULL;
    }
}

esp_err_t bsp_display_lvgl_init(bsp_display_lvgl_ctx_t *ctx)
{
    esp_err_t ret = ESP_OK;
    bool adapter_initialized = false;
    bsp_display_config_t spi_cfg = {
        .max_transfer_sz = BSP_LCD_FULL_FRAME_BYTES,
    };
    bsp_display_parlio_config_t parlio_cfg = {
        .max_transfer_bytes = BSP_LCD_FULL_FRAME_BYTES,
        .dma_burst_size = 0,
        .trans_queue_depth = 0,
    };

    ESP_RETURN_ON_FALSE(ctx != NULL, ESP_ERR_INVALID_ARG, TAG, "ctx is NULL");
    ESP_RETURN_ON_FALSE(!esp_lv_adapter_is_initialized(), ESP_ERR_INVALID_STATE, TAG, "LVGL adapter already initialized");

    memset(ctx, 0, sizeof(*ctx));
    ctx->rotation = ESP_LV_ADAPTER_ROTATE_0;

    ESP_GOTO_ON_ERROR(bsp_display_new(&spi_cfg, &parlio_cfg, &ctx->panel, &ctx->panel_io), err, TAG, "Display init failed");

    esp_lv_adapter_config_t adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    ESP_GOTO_ON_ERROR(esp_lv_adapter_init(&adapter_cfg), err, TAG, "LVGL adapter init failed");
    adapter_initialized = true;

    esp_lv_adapter_display_config_t display_cfg =
        ESP_LV_ADAPTER_DISPLAY_SPI_WITH_PSRAM_DEFAULT_CONFIG(
            ctx->panel, ctx->panel_io,
            BSP_LCD_H_RES, BSP_LCD_V_RES, ctx->rotation);

    ctx->disp = esp_lv_adapter_register_display(&display_cfg);
    ESP_GOTO_ON_FALSE(ctx->disp != NULL, ESP_FAIL, err, TAG, "LVGL display register failed");

    ESP_GOTO_ON_ERROR(bsp_i2c_init(), err, TAG, "I2C init failed");
    ESP_GOTO_ON_ERROR(bsp_touch_new(&ctx->touch_handle), err, TAG, "Touch init failed");

    esp_lv_adapter_touch_config_t touch_cfg =
        ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(ctx->disp, ctx->touch_handle);
    ctx->touch = esp_lv_adapter_register_touch(&touch_cfg);
    ESP_GOTO_ON_FALSE(ctx->touch != NULL, ESP_FAIL, err, TAG, "LVGL touch register failed");

    ESP_GOTO_ON_ERROR(esp_lv_adapter_start(), err, TAG, "LVGL adapter start failed");

    ESP_LOGI(TAG, "Display + LVGL + touch ready (%dx%d)", BSP_LCD_H_RES, BSP_LCD_V_RES);
    return ESP_OK;

err:
    bsp_display_lvgl_cleanup(ctx, adapter_initialized);
    return ret;
}
