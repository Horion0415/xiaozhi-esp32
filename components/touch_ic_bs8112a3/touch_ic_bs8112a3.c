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
#include "freertos/semphr.h"
#include "touch_ic_bs8112a3.h"

static const char *TAG = "touch_ic_bs8112a3";

// Private variables
static i2c_master_dev_handle_t s_bs8112a3_dev_handle = NULL;
static uint8_t s_touch_button_status = 0;
static TaskHandle_t s_status_update_task = NULL;
static bool s_task_running = false;
static bool s_initialized = false;
static gpio_num_t s_interrupt_pin = GPIO_NUM_NC;
static SemaphoreHandle_t s_touch_semaphore = NULL;
static bool s_polling_paused = false;

// Private function declarations
static esp_err_t bs8112a3_read_register(uint8_t reg_addr, uint8_t *data, size_t len);
static esp_err_t bs8112a3_write_register(uint8_t reg_addr, const uint8_t *data, size_t len);
static esp_err_t bs8112a3_configure_chip(void);
static esp_err_t touch_interrupt_init(gpio_num_t pin);
static void IRAM_ATTR touch_interrupt_handler(void *arg);
static void touch_status_update_task(void *pvParameter);

/**
 * @brief Read data from BS8112A3 register
 */
static esp_err_t bs8112a3_read_register(uint8_t reg_addr, uint8_t *data, size_t len)
{
    if (!s_bs8112a3_dev_handle || !data) {
        return ESP_ERR_INVALID_ARG;
    }
    
    return i2c_master_transmit_receive(s_bs8112a3_dev_handle,
                                       &reg_addr, 1,
                                       data, len,
                                       1000);
}

/**
 * @brief Write data to BS8112A3 register
 */
static esp_err_t bs8112a3_write_register(uint8_t reg_addr, const uint8_t *data, size_t len)
{
    if (!s_bs8112a3_dev_handle || !data) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint8_t write_buf[len + 1];
    write_buf[0] = reg_addr;
    memcpy(&write_buf[1], data, len);
    
    return i2c_master_transmit(s_bs8112a3_dev_handle,
                               write_buf, len + 1,
                               1000);
}

/**
 * @brief Configure BS8112A3 chip settings
 */
static esp_err_t bs8112a3_configure_chip(void)
{
    esp_err_t ret;
    
    // Set IRQ mode
    uint8_t irq_mode = 0x00;
    ret = bs8112a3_write_register(BS8112A3_IRQ_MODE_ADDR, &irq_mode, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set IRQ mode");
        return ret;
    }
    
    // Configure key trigger threshold (increased from 50 to 75 for less sensitivity)
    // uint8_t config_data[18] = {0x00, 0x00, 0x83, 0xf3, 0x98, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 0x40, 0};
    uint8_t config_data[18] = {0x00, 0x00, 0x83, 0xf3, 0x98, 35, 35, 35, 35, 35, 35, 35, 35, 35, 35, 35, 0x40, 0};

    // Calculate checksum
    for (int i = 0; i < 17; i++) {
        config_data[17] += config_data[i];
    }
    
    // Write configuration
    ret = bs8112a3_write_register(0xB0, config_data, 18);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write configuration");
        return ret;
    }
    
    ESP_LOGI(TAG, "BS8112A3 chip configured successfully");
    return ESP_OK;
}

/**
 * @brief Initialize GPIO interrupt for BS8112A3
 */
static esp_err_t touch_interrupt_init(gpio_num_t pin)
{
    s_interrupt_pin = pin;

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = gpio_isr_handler_add(pin, touch_interrupt_handler, NULL);
    if (ret != ESP_OK) {
        return ret;
    }

    return ESP_OK;
}

/**
 * @brief GPIO interrupt handler for BS8112A3
 */
static void IRAM_ATTR touch_interrupt_handler(void *arg)
{
    if (s_touch_semaphore) {
        xSemaphoreGiveFromISR(s_touch_semaphore, NULL);
    }
}

/**
 * @brief Task to continuously update touch button status
 */
static void touch_status_update_task(void *pvParameter)
{
    while (s_task_running) {
        if (s_polling_paused) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        
        uint8_t status = 0;
        esp_err_t ret;
        
        if (s_touch_semaphore && s_interrupt_pin != GPIO_NUM_NC) {
            if (xSemaphoreTake(s_touch_semaphore, pdMS_TO_TICKS(30)) == pdTRUE) {
                ret = bs8112a3_read_register(BS8112A3_KEY1_TO_KEY8_STATUS_ADDR, &status, 1);
                if (ret == ESP_OK) {
                    s_touch_button_status = status;
                }
            } else {
                ret = bs8112a3_read_register(BS8112A3_KEY1_TO_KEY8_STATUS_ADDR, &status, 1);
                if (ret == ESP_OK) {
                    s_touch_button_status = status;
                }
            }
        } else {
            ret = bs8112a3_read_register(BS8112A3_KEY1_TO_KEY8_STATUS_ADDR, &status, 1);
            if (ret == ESP_OK) {
                s_touch_button_status = status;
            }
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
    
    vTaskDelete(NULL);
}

/* Public API implementations */

esp_err_t touch_ic_bs8112a3_init(const touch_ic_bs8112a3_config_t *config)
{
    if (!config || !config->i2c_bus_handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (s_initialized) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing BS8112A3 touch IC");
    
    // Create I2C device handle
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = config->device_address,
        .scl_speed_hz = config->scl_speed_hz,
        .flags.disable_ack_check = false,
    };
    
    esp_err_t ret = i2c_master_bus_add_device(config->i2c_bus_handle, &dev_cfg, &s_bs8112a3_dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add I2C device");
        return ret;
    }
    
    // Check hardware availability
    ret = i2c_master_probe(config->i2c_bus_handle, config->device_address, 100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Touch IC hardware not responding");
        i2c_master_bus_rm_device(s_bs8112a3_dev_handle);
        s_bs8112a3_dev_handle = NULL;
        return ESP_ERR_NOT_FOUND;
    }
    
    // Configure chip
    ret = bs8112a3_configure_chip();
    if (ret != ESP_OK) {
        i2c_master_bus_rm_device(s_bs8112a3_dev_handle);
        s_bs8112a3_dev_handle = NULL;
        return ret;
    }

    // Initialize interrupt if pin is provided
    if (config->interrupt_pin != GPIO_NUM_NC) {
        // Install GPIO ISR service if not already installed (required for interrupt mode)
        ret = gpio_install_isr_service(0);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "Failed to install GPIO ISR service: %s", esp_err_to_name(ret));
            i2c_master_bus_rm_device(s_bs8112a3_dev_handle);
            s_bs8112a3_dev_handle = NULL;
            return ret;
        } else if (ret == ESP_ERR_INVALID_STATE) {
            ESP_LOGD(TAG, "GPIO ISR service already installed");
        } else {
            ESP_LOGI(TAG, "GPIO ISR service installed for touch interrupt");
        }
        
        s_touch_semaphore = xSemaphoreCreateBinary();
        if (s_touch_semaphore == NULL) {
            i2c_master_bus_rm_device(s_bs8112a3_dev_handle);
            s_bs8112a3_dev_handle = NULL;
            return ESP_FAIL;
        }
        
        ret = touch_interrupt_init(config->interrupt_pin);
        if (ret != ESP_OK) {
            vSemaphoreDelete(s_touch_semaphore);
            s_touch_semaphore = NULL;
            i2c_master_bus_rm_device(s_bs8112a3_dev_handle);
            s_bs8112a3_dev_handle = NULL;
            return ret;
        }
        ESP_LOGI(TAG, "Touch IC initialized with interrupt mode on GPIO %d", config->interrupt_pin);
    } else {
        ESP_LOGI(TAG, "Touch IC initialized with polling mode");
    }
    
    // Start status update task
    s_task_running = true;
    BaseType_t task_ret = xTaskCreate(touch_status_update_task, 
                                      "touch_status", 
                                      4096,
                                      NULL, 
                                      5, 
                                      &s_status_update_task);
    
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create status update task");
        s_task_running = false;
        if (s_touch_semaphore) {
            vSemaphoreDelete(s_touch_semaphore);
            s_touch_semaphore = NULL;
        }
        if (s_interrupt_pin != GPIO_NUM_NC) {
            gpio_isr_handler_remove(s_interrupt_pin);
            s_interrupt_pin = GPIO_NUM_NC;
        }
        i2c_master_bus_rm_device(s_bs8112a3_dev_handle);
        s_bs8112a3_dev_handle = NULL;
        return ESP_FAIL;
    }
    
    s_initialized = true;
    ESP_LOGI(TAG, "BS8112A3 touch IC initialized successfully");
    return ESP_OK;
}

uint8_t touch_ic_bs8112a3_get_key_status(void)
{
    return s_touch_button_status;
}

bool touch_ic_bs8112a3_get_key_value(uint8_t key_index)
{
    if (key_index >= TOUCH_BUTTON_NUM || !s_initialized) {
        return false;
    }
    
    // Key2-Key7 correspond to bits 2-7 in the status register
    uint8_t bit_position = key_index + 2;
    return (s_touch_button_status >> bit_position) & 0x01;
}

esp_err_t touch_ic_bs8112a3_deinit(void)
{
    if (!s_initialized) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Deinitializing BS8112A3 touch IC");
    
    // Stop status update task
    s_task_running = false;
    if (s_status_update_task) {
        vTaskDelay(pdMS_TO_TICKS(20));
        s_status_update_task = NULL;
    }

    // Remove GPIO interrupt
    if (s_interrupt_pin != GPIO_NUM_NC) {
        gpio_isr_handler_remove(s_interrupt_pin);
        s_interrupt_pin = GPIO_NUM_NC;
    }
    
    // Delete semaphore
    if (s_touch_semaphore) {
        vSemaphoreDelete(s_touch_semaphore);
        s_touch_semaphore = NULL;
    }

    // Remove I2C device
    esp_err_t ret = ESP_OK;
    if (s_bs8112a3_dev_handle) {
        ret = i2c_master_bus_rm_device(s_bs8112a3_dev_handle);
        s_bs8112a3_dev_handle = NULL;
    }
    
    // Reset state
    s_touch_button_status = 0;
    s_initialized = false;
    
    return ret;
}

bool touch_ic_bs8112a3_is_initialized(void)
{
    return s_initialized;
}

void touch_ic_bs8112a3_pause_polling(void)
{
    s_polling_paused = true;
}

void touch_ic_bs8112a3_resume_polling(void)
{
    s_polling_paused = false;
}
