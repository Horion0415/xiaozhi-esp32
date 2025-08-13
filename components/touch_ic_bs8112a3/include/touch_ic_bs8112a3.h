/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BS8112A3_I2C_ADDR                   0x50        /*!< BS8112A3 I2C device address */
#define BS8112A3_KEY1_TO_KEY8_STATUS_ADDR   0x08        /*!< Key status register address */
#define BS8112A3_IRQ_MODE_ADDR              0xB0        /*!< IRQ mode register address */

#define TOUCH_BUTTON_NUM                    (6)         /*!< Number of available touch buttons (Key2-Key7) */

/**
 * @brief Touch IC BS8112A3 configuration structure
 */
typedef struct {
    i2c_master_bus_handle_t i2c_bus_handle; /*!< I2C bus handle from BSP layer */
    uint16_t device_address;                 /*!< I2C device address */
    uint32_t scl_speed_hz;                   /*!< I2C SCL speed in Hz */
    gpio_num_t interrupt_pin;                /*!< GPIO pin for interrupt (optional, set to GPIO_NUM_NC to disable) */
} touch_ic_bs8112a3_config_t;

/**
 * @brief Initialize BS8112A3 touch IC
 * 
 * @param config Configuration structure containing I2C settings
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: Invalid arguments
 *      - ESP_ERR_NOT_FOUND: Hardware not responding
 *      - ESP_FAIL: I2C communication failed
 */
esp_err_t touch_ic_bs8112a3_init(const touch_ic_bs8112a3_config_t *config);

/**
 * @brief Get current touch button status register value
 * 
 * @return 8-bit status register value (bit 2-7 for Key2-Key7)
 */
uint8_t touch_ic_bs8112a3_get_key_status(void);

/**
 * @brief Get specific touch button state
 * 
 * @param key_index Touch button index (0-5 for Key2-Key7)
 * @return
 *      - true: Button is pressed
 *      - false: Button is not pressed
 */
bool touch_ic_bs8112a3_get_key_value(uint8_t key_index);

/**
 * @brief Deinitialize BS8112A3 touch IC
 * 
 * @return
 *      - ESP_OK: Success
 *      - ESP_FAIL: Deinitialization failed
 */
esp_err_t touch_ic_bs8112a3_deinit(void);

/**
 * @brief Check if touch IC is initialized
 * 
 * @return
 *      - true: Touch IC is initialized
 *      - false: Touch IC is not initialized
 */
bool touch_ic_bs8112a3_is_initialized(void);

/**
 * @brief Pause touch status polling temporarily
 */
void touch_ic_bs8112a3_pause_polling(void);

/**
 * @brief Resume touch status polling
 */
void touch_ic_bs8112a3_resume_polling(void);

#ifdef __cplusplus
}
#endif

