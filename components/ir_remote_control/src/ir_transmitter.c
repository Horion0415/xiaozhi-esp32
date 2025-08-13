/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_check.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "ir_transmitter.h"
#include "esp_heap_caps.h"
#include "driver/gptimer.h"

static const char *TAG = "ir_tx_ledc";

typedef struct {
    ledc_timer_config_t ledc_timer_cfg;
    ledc_channel_config_t ledc_chan_cfg;
    uint32_t duty_max;
    gptimer_handle_t gptimer;
    uint32_t *durations_us;
    size_t durations_len;
    size_t index;
    SemaphoreHandle_t tx_done_sem;
    SemaphoreHandle_t tx_lock;
    uint32_t tx_count;
    uint32_t err_count;
    bool initialized;
    bool busy;
} ir_ledc_ctx_t;

static ir_ledc_ctx_t g_ctx = {0};

static void ir_set_carrier_enabled(bool enable);
static bool IRAM_ATTR gptimer_on_alarm_cb(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx);

static inline esp_err_t ir_ledc_set_duty_percent(uint8_t percent)
{
    uint32_t duty = (g_ctx.duty_max * percent) / 100;
    esp_err_t err = ledc_set_duty(g_ctx.ledc_chan_cfg.speed_mode, g_ctx.ledc_chan_cfg.channel, duty);
    if (err != ESP_OK) return err;
    return ledc_update_duty(g_ctx.ledc_chan_cfg.speed_mode, g_ctx.ledc_chan_cfg.channel);
}

static void ir_set_carrier_enabled(bool enable)
{
    if (enable) {
        ir_ledc_set_duty_percent(33);
    } else {
        ir_ledc_set_duty_percent(0);
    }
}

static bool IRAM_ATTR gptimer_on_alarm_cb(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx)
{
    (void)timer;
    (void)user_ctx;
    if (g_ctx.index >= g_ctx.durations_len) {
        ir_set_carrier_enabled(false);
        gptimer_stop(g_ctx.gptimer);
        BaseType_t hp = pdFALSE;
        if (g_ctx.tx_done_sem) xSemaphoreGiveFromISR(g_ctx.tx_done_sem, &hp);
        return true;
    }
    bool on = ((g_ctx.index % 2) == 0);
    ir_set_carrier_enabled(on);
    uint32_t next = g_ctx.durations_us[g_ctx.index++];
    gptimer_alarm_config_t alarm_cfg = {
        .alarm_count = edata->count_value + next,
        .reload_count = 0,
        .flags.auto_reload_on_alarm = false,
    };
    gptimer_set_alarm_action(g_ctx.gptimer, &alarm_cfg);
    return true;
}

static void ir_debug_analyze_timings(const uint32_t *timing_data, size_t length)
{
    if (!timing_data || length < 2) return;
}

esp_err_t ir_transmitter_init(const ir_tx_config_t *config)
{
    ESP_RETURN_ON_FALSE(config, ESP_ERR_INVALID_ARG, TAG, "invalid config");
    ESP_RETURN_ON_FALSE(!g_ctx.initialized, ESP_ERR_INVALID_STATE, TAG, "already initialized");

    memset(&g_ctx, 0, sizeof(g_ctx));

    g_ctx.ledc_timer_cfg = (ledc_timer_config_t){
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = config->carrier_freq_hz ? config->carrier_freq_hz : 38000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&g_ctx.ledc_timer_cfg), TAG, "ledc timer config failed");

    g_ctx.ledc_chan_cfg = (ledc_channel_config_t){
        .gpio_num = (int)config->tx_gpio,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0,
        .flags.output_invert = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&g_ctx.ledc_chan_cfg), TAG, "ledc channel config failed");

    g_ctx.duty_max = (1u << g_ctx.ledc_timer_cfg.duty_resolution) - 1u;

    gptimer_config_t tcfg = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000,
    };
    ESP_RETURN_ON_ERROR(gptimer_new_timer(&tcfg, &g_ctx.gptimer), TAG, "gptimer new failed");
    gptimer_event_callbacks_t cbs = { .on_alarm = gptimer_on_alarm_cb };
    ESP_RETURN_ON_ERROR(gptimer_register_event_callbacks(g_ctx.gptimer, &cbs, NULL), TAG, "gptimer cb failed");
    ESP_RETURN_ON_ERROR(gptimer_enable(g_ctx.gptimer), TAG, "gptimer enable failed");

    g_ctx.tx_done_sem = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(g_ctx.tx_done_sem != NULL, ESP_ERR_NO_MEM, TAG, "no mem for sem");
    g_ctx.tx_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(g_ctx.tx_lock != NULL, ESP_ERR_NO_MEM, TAG, "no mem for mutex");

    g_ctx.initialized = true;
    ESP_LOGI(TAG, "IR TX initialized: GPIO=%u, carrier=%u Hz", (unsigned)config->tx_gpio, (unsigned)g_ctx.ledc_timer_cfg.freq_hz);
    return ESP_OK;
}

esp_err_t ir_transmitter_deinit(void)
{
    if (!g_ctx.initialized) return ESP_OK;

    if (g_ctx.gptimer) {
        gptimer_disable(g_ctx.gptimer);
        gptimer_del_timer(g_ctx.gptimer);
        g_ctx.gptimer = NULL;
    }

    ir_set_carrier_enabled(false);
    ledc_stop(g_ctx.ledc_chan_cfg.speed_mode, g_ctx.ledc_chan_cfg.channel, 0);

    if (g_ctx.tx_done_sem) {
        vSemaphoreDelete(g_ctx.tx_done_sem);
        g_ctx.tx_done_sem = NULL;
    }
    if (g_ctx.tx_lock) {
        vSemaphoreDelete(g_ctx.tx_lock);
        g_ctx.tx_lock = NULL;
    }

    if (g_ctx.durations_us) {
        free(g_ctx.durations_us);
        g_ctx.durations_us = NULL;
    }

    memset(&g_ctx, 0, sizeof(g_ctx));
    return ESP_OK;
}

esp_err_t ir_transmitter_send_timing_data(const uint32_t *timing_data, size_t length)
{
    ESP_RETURN_ON_FALSE(g_ctx.initialized, ESP_ERR_INVALID_STATE, TAG, "not initialized");
    ESP_RETURN_ON_FALSE(timing_data && length > 0, ESP_ERR_INVALID_ARG, TAG, "invalid data");

    ESP_LOGI(TAG, "TX request: len=%u%s", (unsigned)length, (length % 2) ? " (odd)" : "");

    ir_debug_analyze_timings(timing_data, length);

    if (length % 2 != 0) {
        length -= 1;
        ESP_LOGW(TAG, "TX length adjusted to even: %u", (unsigned)length);
    }
    if (length < 2) {
        ESP_LOGW(TAG, "TX timings too short after adjust: %u", (unsigned)length);
    }
    ESP_RETURN_ON_FALSE(length >= 2, ESP_ERR_INVALID_ARG, TAG, "insufficient timing data");

    if (xSemaphoreTake(g_ctx.tx_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_INVALID_STATE;
    }

    if (g_ctx.durations_us) {
        free(g_ctx.durations_us);
        g_ctx.durations_us = NULL;
    }
    g_ctx.durations_us = (uint32_t *)heap_caps_malloc(length * sizeof(uint32_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!g_ctx.durations_us) {
        xSemaphoreGive(g_ctx.tx_lock);
        return ESP_ERR_NO_MEM;
    }
    memcpy(g_ctx.durations_us, timing_data, length * sizeof(uint32_t));
    g_ctx.durations_len = length;
    g_ctx.index = 1;

    g_ctx.busy = true;
    ir_set_carrier_enabled(true);

    while (xSemaphoreTake(g_ctx.tx_done_sem, 0) == pdTRUE) {
    }

    gptimer_set_raw_count(g_ctx.gptimer, 0);
    gptimer_alarm_config_t alarm_cfg = {
        .alarm_count = timing_data[0],
        .reload_count = 0,
        .flags.auto_reload_on_alarm = false,
    };
    esp_err_t err = gptimer_set_alarm_action(g_ctx.gptimer, &alarm_cfg);
    if (err != ESP_OK) {
        g_ctx.busy = false;
        xSemaphoreGive(g_ctx.tx_lock);
        return err;
    }
    err = gptimer_start(g_ctx.gptimer);
    if (err != ESP_OK) {
        g_ctx.busy = false;
        xSemaphoreGive(g_ctx.tx_lock);
        return err;
    }

    if (xSemaphoreTake(g_ctx.tx_done_sem, pdMS_TO_TICKS(3000)) != pdTRUE) {
        gptimer_stop(g_ctx.gptimer);
        ir_set_carrier_enabled(false);
        g_ctx.busy = false;
        g_ctx.err_count++;
        xSemaphoreGive(g_ctx.tx_lock);
        ESP_LOGE(TAG, "IR TX timeout");
        return ESP_ERR_TIMEOUT;
    }

    g_ctx.tx_count++;
    g_ctx.busy = false;
    xSemaphoreGive(g_ctx.tx_lock);
    ESP_LOGI(TAG, "IR frame sent");
    return ESP_OK;
}

bool ir_transmitter_is_ready(void)
{
    return g_ctx.initialized && !g_ctx.busy;
}

esp_err_t ir_transmitter_get_status(uint32_t *tx_count, uint32_t *error_count)
{
    ESP_RETURN_ON_FALSE(tx_count && error_count, ESP_ERR_INVALID_ARG, TAG, "invalid args");
    *tx_count = g_ctx.tx_count;
    *error_count = g_ctx.err_count;
    return ESP_OK;
} 