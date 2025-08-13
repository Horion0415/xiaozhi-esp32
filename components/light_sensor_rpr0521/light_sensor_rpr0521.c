/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "light_sensor_rpr0521.h"

static const char *TAG = "light_sensor_rpr0521";

/* Private variables */
static i2c_master_dev_handle_t s_rpr0521_dev_handle = NULL;
static bool s_initialized = false;
static gpio_num_t s_interrupt_pin = GPIO_NUM_NC;
static uint8_t s_current_mode_control = 0x00;
static uint8_t s_current_als_ps_control = 0x02;

/* Private function declarations */
static esp_err_t rpr0521_read_register(uint8_t reg_addr, uint8_t *data, size_t len);
static esp_err_t rpr0521_write_register(uint8_t reg_addr, const uint8_t *data, size_t len);
static esp_err_t rpr0521_configure_chip(void);
static esp_err_t rpr0521_read_16bit_register(uint8_t lsb_reg, uint8_t msb_reg, uint16_t *value);

/**
 * @brief Read data from RPR0521 register
 */
static esp_err_t rpr0521_read_register(uint8_t reg_addr, uint8_t *data, size_t len)
{
    if (!s_rpr0521_dev_handle || !data) {
        return ESP_ERR_INVALID_ARG;
    }
    
    return i2c_master_transmit_receive(s_rpr0521_dev_handle,
                                       &reg_addr, 1,
                                       data, len,
                                       1000 / portTICK_PERIOD_MS);
}

/**
 * @brief Write data to RPR0521 register
 */
static esp_err_t rpr0521_write_register(uint8_t reg_addr, const uint8_t *data, size_t len)
{
    if (!s_rpr0521_dev_handle || !data) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint8_t write_buf[len + 1];
    write_buf[0] = reg_addr;
    memcpy(&write_buf[1], data, len);
    
    return i2c_master_transmit(s_rpr0521_dev_handle,
                               write_buf, len + 1,
                               1000 / portTICK_PERIOD_MS);
}

/**
 * @brief Read 16-bit value from two consecutive registers
 */
static esp_err_t rpr0521_read_16bit_register(uint8_t lsb_reg, uint8_t msb_reg, uint16_t *value)
{
    if (!value) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint8_t lsb_data, msb_data;
    esp_err_t ret;
    
    /* Read LSB first */
    ret = rpr0521_read_register(lsb_reg, &lsb_data, 1);
    if (ret != ESP_OK) {
        return ret;
    }
    
    /* Read MSB */
    ret = rpr0521_read_register(msb_reg, &msb_data, 1);
    if (ret != ESP_OK) {
        return ret;
    }
    
    *value = ((uint16_t)msb_data << 8) | lsb_data;
    return ESP_OK;
}

/**
 * @brief Configure RPR0521 chip with default settings
 */
static esp_err_t rpr0521_configure_chip(void)
{
    esp_err_t ret;
    
    /* Configure ALS_PS_CONTROL register
     * ALS DATA0 GAIN: x1 (bits 5:4 = 00)
     * ALS DATA1 GAIN: x1 (bits 3:2 = 00)
     * LED CURRENT: 100mA (bits 1:0 = 10)
     */
    s_current_als_ps_control = 0x02;
    ret = rpr0521_write_register(RPR0521_ALS_PS_CONTROL_REG, &s_current_als_ps_control, 1);
    if (ret != ESP_OK) {
        return ret;
    }
    
    /* Wait for register configuration to settle */
    vTaskDelay(pdMS_TO_TICKS(10));

    /* Configure MODE_CONTROL register
     * ALS_EN: ALS measurement on (bit 7 = 1)
     * PS_EN: PS standby (bit 6 = 0)
     * PS_PULSE: PS LED pulse width typ:200us (bits 5:4 = 00)
     * PS Operating mode: Normal mode (bit 3 = 1)
     * Measurement time: ALS(400ms) PS(standby) (bits 2:0 = 010)
     */
    s_current_mode_control = 0x8A;
    ret = rpr0521_write_register(RPR0521_MODE_CONTROL_REG, &s_current_mode_control, 1);
    if (ret != ESP_OK) {
        return ret;
    }

    /* Wait for first measurement to complete */
    vTaskDelay(pdMS_TO_TICKS(500));

    return ESP_OK;
}

/* Public API implementations */

esp_err_t light_sensor_rpr0521_init(const light_sensor_rpr0521_config_t *config)
{
    if (!config || !config->i2c_bus_handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (s_initialized) {
        return ESP_OK;
    }
    
    /* Configure I2C device */
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = config->device_address,
        .scl_speed_hz = config->scl_speed_hz,
    };
    
    esp_err_t ret = i2c_master_bus_add_device(config->i2c_bus_handle, &dev_cfg, &s_rpr0521_dev_handle);
    if (ret != ESP_OK) {
        return ret;
    }
    
    /* Test device communication by reading mode control register */
    uint8_t test_data;
    ret = rpr0521_read_register(RPR0521_MODE_CONTROL_REG, &test_data, 1);
    if (ret != ESP_OK) {
        i2c_master_bus_rm_device(s_rpr0521_dev_handle);
        s_rpr0521_dev_handle = NULL;
        return ESP_ERR_NOT_FOUND;
    }
    
    /* Configure the chip with default settings */
    ret = rpr0521_configure_chip();
    if (ret != ESP_OK) {
        i2c_master_bus_rm_device(s_rpr0521_dev_handle);
        s_rpr0521_dev_handle = NULL;
        return ret;
    }
    
    s_interrupt_pin = config->interrupt_pin;
    s_initialized = true;
    
    return ESP_OK;
}

esp_err_t light_sensor_rpr0521_read_als_data0(uint16_t *als_value)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!als_value) {
        return ESP_ERR_INVALID_ARG;
    }
    
    return rpr0521_read_16bit_register(RPR0521_ALS_DATA0_LSB_REG, RPR0521_ALS_DATA0_MSB_REG, als_value);
}

esp_err_t light_sensor_rpr0521_read_als_data1(uint16_t *als_value)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!als_value) {
        return ESP_ERR_INVALID_ARG;
    }
    
    return rpr0521_read_16bit_register(RPR0521_ALS_DATA1_LSB_REG, RPR0521_ALS_DATA1_MSB_REG, als_value);
}

esp_err_t light_sensor_rpr0521_read_ps(uint16_t *ps_value)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!ps_value) {
        return ESP_ERR_INVALID_ARG;
    }
    
    return rpr0521_read_16bit_register(RPR0521_PS_DATA_LSB_REG, RPR0521_PS_DATA_MSB_REG, ps_value);
}

esp_err_t light_sensor_rpr0521_set_als_gain(rpr0521_als_gain_t gain_data0, rpr0521_als_gain_t gain_data1)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (gain_data0 > RPR0521_ALS_GAIN_128X || gain_data1 > RPR0521_ALS_GAIN_128X) {
        return ESP_ERR_INVALID_ARG;
    }
    
    /* Update ALS_PS_CONTROL register with new gain values
     * Keep LED current setting (bits 1:0), update gain settings
     */
    s_current_als_ps_control = (s_current_als_ps_control & 0x03) | 
                               (gain_data0 << 4) | (gain_data1 << 2);
    
    return rpr0521_write_register(RPR0521_ALS_PS_CONTROL_REG, &s_current_als_ps_control, 1);
}

esp_err_t light_sensor_rpr0521_set_measurement_time(rpr0521_measurement_time_t mtime)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (mtime < RPR0521_MTIME_100MS || mtime > RPR0521_MTIME_800MS) {
        return ESP_ERR_INVALID_ARG;
    }
    
    /* Update MODE_CONTROL register with new measurement time
     * Keep other settings, update measurement time (bits 2:0)
     */
    s_current_mode_control = (s_current_mode_control & 0xF8) | (mtime & 0x07);
    
    return rpr0521_write_register(RPR0521_MODE_CONTROL_REG, &s_current_mode_control, 1);
}

esp_err_t light_sensor_rpr0521_enable_als(bool enable)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    /* Update MODE_CONTROL register ALS_EN bit (bit 7) */
    if (enable) {
        s_current_mode_control |= 0x80;  /* Set bit 7 */
    } else {
        s_current_mode_control &= ~0x80; /* Clear bit 7 */
    }
    
    esp_err_t ret = rpr0521_write_register(RPR0521_MODE_CONTROL_REG, &s_current_mode_control, 1);
    
    if (ret == ESP_OK && enable) {
        /* Wait for measurement to stabilize when enabling */
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    return ret;
}

esp_err_t light_sensor_rpr0521_enable_ps(bool enable)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    /* Update MODE_CONTROL register PS_EN bit (bit 6) */
    if (enable) {
        s_current_mode_control |= 0x40;  /* Set bit 6 */
    } else {
        s_current_mode_control &= ~0x40; /* Clear bit 6 */
    }
    
    esp_err_t ret = rpr0521_write_register(RPR0521_MODE_CONTROL_REG, &s_current_mode_control, 1);
    
    if (ret == ESP_OK && enable) {
        /* Wait for measurement to stabilize when enabling */
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    return ret;
}

esp_err_t light_sensor_rpr0521_deinit(void)
{
    if (!s_initialized) {
        return ESP_OK;
    }
    
    /* Disable all measurements */
    uint8_t mode_off = 0x00;
    rpr0521_write_register(RPR0521_MODE_CONTROL_REG, &mode_off, 1);
    
    /* Remove I2C device */
    if (s_rpr0521_dev_handle) {
        esp_err_t ret = i2c_master_bus_rm_device(s_rpr0521_dev_handle);
        s_rpr0521_dev_handle = NULL;
        if (ret != ESP_OK) {
            return ret;
        }
    }
    
    /* Reset state */
    s_initialized = false;
    s_interrupt_pin = GPIO_NUM_NC;
    s_current_mode_control = 0x00;
    s_current_als_ps_control = 0x02;
    
    return ESP_OK;
}

bool light_sensor_rpr0521_is_initialized(void)
{
    return s_initialized;
} 