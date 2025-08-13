/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/* RPR-0521 I2C device address and register definitions */
#define RPR0521_I2C_ADDR                   0x38        /*!< RPR-0521 I2C device address */
#define RPR0521_ALS_DATA0_LSB_REG          0x46        /*!< ALS DATA0 LSB register */
#define RPR0521_ALS_DATA0_MSB_REG          0x47        /*!< ALS DATA0 MSB register */
#define RPR0521_ALS_DATA1_LSB_REG          0x48        /*!< ALS DATA1 LSB register */
#define RPR0521_ALS_DATA1_MSB_REG          0x49        /*!< ALS DATA1 MSB register */
#define RPR0521_PS_DATA_LSB_REG            0x44        /*!< PS DATA LSB register */
#define RPR0521_PS_DATA_MSB_REG            0x45        /*!< PS DATA MSB register */
#define RPR0521_MODE_CONTROL_REG           0x41        /*!< Mode control register */
#define RPR0521_ALS_PS_CONTROL_REG         0x42        /*!< ALS/PS control register */
#define RPR0521_ALS_PS_STATUS_REG          0x43        /*!< ALS/PS status register */

/**
 * @brief Light sensor RPR0521 configuration structure
 */
typedef struct {
    i2c_master_bus_handle_t i2c_bus_handle; /*!< I2C bus handle from BSP layer */
    uint16_t device_address;                 /*!< I2C device address (0x38) */
    uint32_t scl_speed_hz;                   /*!< I2C SCL speed in Hz */
    gpio_num_t interrupt_pin;                /*!< GPIO pin for interrupt (optional, GPIO_NUM_NC to disable) */
} light_sensor_rpr0521_config_t;

/**
 * @brief ALS gain settings
 */
typedef enum {
    RPR0521_ALS_GAIN_1X = 0,    /*!< ALS gain x1 */
    RPR0521_ALS_GAIN_2X,        /*!< ALS gain x2 */
    RPR0521_ALS_GAIN_64X,       /*!< ALS gain x64 */
    RPR0521_ALS_GAIN_128X,      /*!< ALS gain x128 */
} rpr0521_als_gain_t;

/**
 * @brief Measurement time settings
 */
typedef enum {
    RPR0521_MTIME_100MS = 0x06, /*!< Measurement time 100ms */
    RPR0521_MTIME_200MS = 0x07, /*!< Measurement time 200ms */
    RPR0521_MTIME_400MS = 0x08, /*!< Measurement time 400ms (default) */
    RPR0521_MTIME_800MS = 0x09, /*!< Measurement time 800ms */
} rpr0521_measurement_time_t;

/**
 * @brief Initialize RPR-0521 light sensor
 * 
 * @param config Configuration structure containing I2C settings
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: Invalid arguments
 *      - ESP_ERR_NOT_FOUND: Hardware not responding
 *      - ESP_FAIL: I2C communication failed
 */
esp_err_t light_sensor_rpr0521_init(const light_sensor_rpr0521_config_t *config);

/**
 * @brief Read ambient light sensor DATA0 value
 * 
 * @param als_value Pointer to store ALS DATA0 value
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: Invalid arguments
 *      - ESP_ERR_INVALID_STATE: Sensor not initialized
 *      - ESP_FAIL: Read operation failed
 */
esp_err_t light_sensor_rpr0521_read_als_data0(uint16_t *als_value);

/**
 * @brief Read ambient light sensor DATA1 value
 * 
 * @param als_value Pointer to store ALS DATA1 value
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: Invalid arguments
 *      - ESP_ERR_INVALID_STATE: Sensor not initialized
 *      - ESP_FAIL: Read operation failed
 */
esp_err_t light_sensor_rpr0521_read_als_data1(uint16_t *als_value);

/**
 * @brief Read proximity sensor value
 * 
 * @param ps_value Pointer to store PS value
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: Invalid arguments
 *      - ESP_ERR_INVALID_STATE: Sensor not initialized
 *      - ESP_FAIL: Read operation failed
 */
esp_err_t light_sensor_rpr0521_read_ps(uint16_t *ps_value);

/**
 * @brief Set ALS gain for both DATA0 and DATA1
 * 
 * @param gain_data0 ALS gain setting for DATA0
 * @param gain_data1 ALS gain setting for DATA1
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: Invalid gain value
 *      - ESP_ERR_INVALID_STATE: Sensor not initialized
 *      - ESP_FAIL: Configuration failed
 */
esp_err_t light_sensor_rpr0521_set_als_gain(rpr0521_als_gain_t gain_data0, rpr0521_als_gain_t gain_data1);

/**
 * @brief Set measurement time
 * 
 * @param mtime Measurement time setting
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: Invalid measurement time
 *      - ESP_ERR_INVALID_STATE: Sensor not initialized
 *      - ESP_FAIL: Configuration failed
 */
esp_err_t light_sensor_rpr0521_set_measurement_time(rpr0521_measurement_time_t mtime);

/**
 * @brief Enable/disable ALS measurement
 * 
 * @param enable true to enable, false to disable
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_STATE: Sensor not initialized
 *      - ESP_FAIL: Configuration failed
 */
esp_err_t light_sensor_rpr0521_enable_als(bool enable);

/**
 * @brief Enable/disable PS measurement
 * 
 * @param enable true to enable, false to disable
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_STATE: Sensor not initialized
 *      - ESP_FAIL: Configuration failed
 */
esp_err_t light_sensor_rpr0521_enable_ps(bool enable);

/**
 * @brief Deinitialize RPR-0521 light sensor
 * 
 * @return
 *      - ESP_OK: Success
 *      - ESP_FAIL: Deinitialization failed
 */
esp_err_t light_sensor_rpr0521_deinit(void);

/**
 * @brief Check if light sensor is initialized
 * 
 * @return
 *      - true: Sensor is initialized
 *      - false: Sensor is not initialized
 */
bool light_sensor_rpr0521_is_initialized(void);

#ifdef __cplusplus
}
#endif 