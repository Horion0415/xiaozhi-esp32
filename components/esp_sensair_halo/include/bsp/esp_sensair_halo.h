/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief ESP BSP: SensairHalo Board Support Package
 *
 * Board: ESP-SensairHalo (ESP32-C5, MainBoard V0.6)
 *
 * Provides unified hardware access for:
 *   - ILI9341 LCD (284x240, SPI/PARLIO)
 *   - CST816S touch panel (I2C)
 *   - BS8112A3 touch buttons (I2C)
 *   - PDM speaker + ADC microphone
 *   - BMI270 IMU + BMM150 magnetometer (LP I2C, legacy i2c_bus)
 *   - WS2812 LED strip (RMT)
 *   - BF3901 SPI camera (V4L2)
 */

#pragma once

#include "sdkconfig.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_lcd_types.h"
#include "esp_lcd_touch.h"
#include "esp_codec_dev.h"
#include "led_strip.h"
#include "touch_ic_bs8112a3.h"
#include "bmi270.h"
#include "bmm150_aux_adapter.h"
#include "bsp/display.h"

/**************************************************************************************************
 *  BSP Capabilities
 **************************************************************************************************/

#define BSP_CAPS_DISPLAY         1
#define BSP_CAPS_TOUCH           1
#define BSP_CAPS_TOUCH_BUTTON    1
#define BSP_CAPS_AUDIO_SPEAKER   1
#define BSP_CAPS_AUDIO_MIC       1
#define BSP_CAPS_IMU             1
#define BSP_CAPS_MAGNETOMETER    1
#define BSP_CAPS_LED_STRIP       1
#define BSP_CAPS_CAMERA          1

/**************************************************************************************************
 * ESP-SensairHalo Pinout
 **************************************************************************************************/

/* I2C (LP_I2C_NUM_0) — shared by touch, touch button, camera SCCB */
#define BSP_I2C_SDA              (GPIO_NUM_2)
#define BSP_I2C_SCL              (GPIO_NUM_3)
#define BSP_I2C_CLK_SPEED_HZ     (100000)   /*!< LP I2C default clock: 100kHz (standard mode) */

/* LCD (SPI2 / PARLIO) */
#define BSP_LCD_SPI_MOSI         (GPIO_NUM_0)
#define BSP_LCD_SPI_CLK          (GPIO_NUM_1)
#define BSP_LCD_SPI_CS           (GPIO_NUM_6)
#define BSP_LCD_DC               (GPIO_NUM_7)

/* Touch Button */
#define BSP_TOUCH_BUTTON_IRQ     (GPIO_NUM_5)

/* Audio */
#define BSP_SPEAKER_P            (GPIO_NUM_9)
#define BSP_SPEAKER_N            (GPIO_NUM_10)
#define BSP_PA_CTRL              (GPIO_NUM_8)

/* IMU (BMI270, on LP I2C same pins as BSP_I2C_SDA/SCL) */
#define BSP_IMU_INT              (GPIO_NUM_28)

/* LED Strip (WS2812) */
#define BSP_LED_STRIP_GPIO       (GPIO_NUM_26)
#define BSP_LED_STRIP_MAX_LEDS   1

/* Camera (BF3901, SPI) */
#define BSP_CAM_SPI_CS           (GPIO_NUM_27)
#define BSP_CAM_SPI_SCLK         (GPIO_NUM_25)
#define BSP_CAM_SPI_DATA0        (GPIO_NUM_23)
#define BSP_CAM_XCLK             (GPIO_NUM_24)
#define BSP_CAM_XCLK_FREQ_HZ    (24000000)

#ifdef __cplusplus
extern "C" {
#endif

/**************************************************************************************************
 * Board Init
 **************************************************************************************************/

/**
 * @brief Initialize board-level resources (PA GPIO, etc.)
 * @return ESP_OK on success
 */
esp_err_t bsp_board_init(void);

/**************************************************************************************************
 * I2C
 *
 * LP I2C bus shared by touch, touch button, and camera SCCB.
 **************************************************************************************************/

#define BSP_I2C_NUM     LP_I2C_NUM_0

esp_err_t bsp_i2c_init(void);
esp_err_t bsp_i2c_deinit(void);
esp_err_t bsp_i2c_get_bus_handle(i2c_master_bus_handle_t *handle);

/**************************************************************************************************
 * Touch (CST816S)
 **************************************************************************************************/

/**
 * @brief Create touch panel driver
 *
 * @note  Requires I2C to be initialized first (bsp_i2c_init)
 * @param[out] ret_touch Touch handle
 * @return ESP_OK on success
 */
esp_err_t bsp_touch_new(esp_lcd_touch_handle_t *ret_touch);

/**************************************************************************************************
 * Touch Button (BS8112A3)
 **************************************************************************************************/

/**
 * @brief Initialize BS8112A3 capacitive touch button IC
 *
 * @note  Requires I2C to be initialized first (bsp_i2c_init)
 * @param[out] ret_handle Touch button handle
 * @return ESP_OK on success
 */
esp_err_t bsp_touch_button_init(touch_ic_bs8112a3_handle_t *ret_handle);

/**************************************************************************************************
 * Audio — Speaker (I2S PDM TX)
 **************************************************************************************************/

/**
 * @brief Initialize PDM speaker and create codec device
 *
 * @param[out] ret_handle Codec device handle for playback
 * @return ESP_OK on success
 */
esp_err_t bsp_speaker_init(esp_codec_dev_handle_t *ret_handle);
esp_err_t bsp_speaker_deinit(void);

/**
 * @brief Enable/disable PA amplifier
 */
esp_err_t bsp_pa_enable(bool enable);

/**************************************************************************************************
 * Audio — Microphone (ADC)
 **************************************************************************************************/

/**
 * @brief Initialize ADC microphone and create codec device
 *
 * @param[out] ret_handle Codec device handle for recording
 * @return ESP_OK on success
 */
esp_err_t bsp_microphone_init(esp_codec_dev_handle_t *ret_handle);
esp_err_t bsp_microphone_deinit(void);

/**************************************************************************************************
 * IMU — BMI270 (Accel + Gyro)
 *
 * The BMI270 is on the same LP I2C bus as touch. BSP handles I2C bus sharing internally.
 **************************************************************************************************/

/**
 * @brief Initialize BMI270 IMU with default settings (100Hz, Accel 4G, Gyro 2000dps)
 *
 * @note  Requires I2C to be initialized first (bsp_i2c_init)
 * @param[out] ret_handle BMI270 sensor handle
 * @return ESP_OK on success
 */
esp_err_t bsp_imu_init(bmi270_handle_t *ret_handle);
esp_err_t bsp_imu_deinit(void);

/**
 * @brief Get IMU sensor handle (for direct bmi2_* API calls)
 */
bmi270_handle_t bsp_imu_get_handle(void);

/**
 * @brief Enable step counter, step detector and step activity recognition
 *
 * Must be called after bsp_imu_init(). Enables BMI270 features:
 *   - Step detector  (event: each step)
 *   - Step counter   (cumulative step count)
 *   - Step activity  (still / walking / running / unknown)
 *
 * @return ESP_OK on success
 */
esp_err_t bsp_imu_enable_step_counter(void);

/**
 * @brief Enable wrist gesture recognition
 *
 * Must be called after bsp_imu_init(). Enables BMI270 wrist gesture feature.
 * Detected gestures: push_arm_down / pivot_up / wrist_shake_jiggle /
 *                    flick_in / flick_out
 *
 * @param[in] wearable_arm  0 = left arm (default), 1 = right arm
 * @return ESP_OK on success
 */
esp_err_t bsp_imu_enable_wrist_gesture(uint8_t wearable_arm);

/**
 * @brief Enable any-motion and no-motion detection
 *
 * Must be called after bsp_imu_init().
 * Triggers when acceleration change exceeds threshold (any-motion)
 * or stays below threshold for duration (no-motion).
 *
 * @return ESP_OK on success
 */
esp_err_t bsp_imu_enable_motion_detect(void);

/**
 * @brief Enable significant-motion detection
 *
 * Must be called after bsp_imu_init().
 * Triggers when the device stays in motion for a sustained period.
 *
 * @return ESP_OK on success
 */
esp_err_t bsp_imu_enable_sig_motion(void);

/**
 * @brief Enable wrist wear wake-up detection
 *
 * Must be called after bsp_imu_init().
 * Triggers when the device is tilted into a watch-viewing position.
 *
 * @return ESP_OK on success
 */
esp_err_t bsp_imu_enable_wrist_wear_wakeup(void);

/**
 * @brief Read step counter value
 *
 * @param[out] step_count  Cumulative step count since last reset
 * @return ESP_OK on success
 */
esp_err_t bsp_imu_get_step_count(uint32_t *step_count);

/**
 * @brief Read step activity classification
 *
 * @param[out] activity  0=still, 1=walking, 2=running, 3=unknown
 * @param[out] activity_str  Human-readable string (static, do not free)
 * @return ESP_OK on success
 */
esp_err_t bsp_imu_get_step_activity(uint8_t *activity, const char **activity_str);

/**
 * @brief Read wrist gesture output
 *
 * @param[out] gesture  Raw gesture value (0-5)
 * @param[out] gesture_str  Human-readable string (static, do not free)
 *   0 = unknown_gesture
 *   1 = push_arm_down
 *   2 = pivot_up
 *   3 = wrist_shake_jiggle
 *   4 = flick_in
 *   5 = flick_out
 * @return ESP_OK on success
 */
esp_err_t bsp_imu_get_wrist_gesture(uint8_t *gesture, const char **gesture_str);

/**
 * @brief Read raw BMI270 feature interrupt status bits
 *
 * Uses masks from bmi270.h, for example:
 *   - BMI270_SIG_MOT_STATUS_MASK
 *   - BMI270_STEP_CNT_STATUS_MASK
 *   - BMI270_STEP_ACT_STATUS_MASK
 *   - BMI270_WRIST_WAKE_UP_STATUS_MASK
 *   - BMI270_WRIST_GEST_STATUS_MASK
 *   - BMI270_NO_MOT_STATUS_MASK
 *   - BMI270_ANY_MOT_STATUS_MASK
 *
 * @param[out] int_status  Raw feature interrupt status bits
 * @return ESP_OK on success
 */
esp_err_t bsp_imu_get_event_status(uint16_t *int_status);

/**
 * @brief Read motion interrupt status (any-motion / no-motion)
 *
 * Reads BMI270 interrupt status register and returns which motion events
 * are currently active.
 *
 * @param[out] any_motion_triggered  true if any-motion interrupt fired
 * @param[out] no_motion_triggered   true if no-motion interrupt fired
 * @return ESP_OK on success
 */
esp_err_t bsp_imu_get_motion_status(bool *any_motion_triggered, bool *no_motion_triggered);

/**************************************************************************************************
 * Magnetometer — BMM150 (via BMI270 AUX interface)
 *
 * The BMM150 is accessed through the BMI270's secondary I2C (AUX) interface,
 * not directly on the I2C bus.
 **************************************************************************************************/

/**
 * @brief Initialize BMM150 magnetometer via BMI270 AUX interface
 *
 * @note  Requires bsp_imu_init() to be called first
 * @param[out] ret_handle BMM150 AUX adapter handle
 * @return ESP_OK on success
 */
esp_err_t bsp_mag_init(bmm150_aux_handle_t *ret_handle);
esp_err_t bsp_mag_deinit(void);

/**************************************************************************************************
 * LED Strip (WS2812)
 **************************************************************************************************/

/**
 * @brief Initialize WS2812 LED strip
 *
 * @param[out] ret_handle LED strip handle
 * @return ESP_OK on success
 */
esp_err_t bsp_led_strip_init(led_strip_handle_t *ret_handle);
esp_err_t bsp_led_strip_deinit(void);

/**************************************************************************************************
 * Camera (BF3901, SPI)
 *
 * After initialization, use V4L2 API with the device path from bsp_camera_get_dev_path().
 **************************************************************************************************/

/**
 * @brief Initialize SPI camera (BF3901) via esp_video
 *
 * @note  Requires I2C to be initialized first (bsp_i2c_init)
 * @return ESP_OK on success
 */
esp_err_t bsp_camera_init(void);
esp_err_t bsp_camera_deinit(void);

/**
 * @brief Apply BSP default camera controls to an opened V4L2 fd
 *
 * @note  The caller owns the fd lifecycle. This API only applies board default
 *        sensor settings such as mirror/flip.
 *
 * @param[in] fd Opened V4L2 camera file descriptor
 * @return ESP_OK on success
 */
esp_err_t bsp_camera_apply_default_controls(int fd);

/**
 * @brief Get V4L2 device path for opened camera
 *
 * @return Device path string (e.g. "/dev/video0"), or NULL if not initialized
 */
const char *bsp_camera_get_dev_path(void);

#ifdef __cplusplus
}
#endif
