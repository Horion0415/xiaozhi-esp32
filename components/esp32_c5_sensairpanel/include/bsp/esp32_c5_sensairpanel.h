/*
 * SPDX-FileCopyrightText: 2023-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */


/**
 * @file
 * @brief ESP BSP: ESP32-C3-LCDkit
 */

#pragma once

#include "driver/gpio.h"
#include "driver/i2s_pdm.h"
#include "driver/i2c_master.h"
#include "iot_button.h"
#include "button_gpio.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "esp_codec_dev.h"
#include "adc_mic.h"
#include "audio_player.h"
#include "file_iterator.h"
#include "bsp/display.h"
#include "ir_keymap.h"
#include "esp_sleep.h"

/**************************************************************************************************
 *  ESP32-C5-Sensairpanel pinout
 **************************************************************************************************/

/* Boot Button */
#define BSP_BOOT_BUTTON           (GPIO_NUM_28)

/* Power Control */
#define BSP_POWER_CTRL            (GPIO_NUM_7)

/* I2C */
#define BSP_I2C_SDA               (GPIO_NUM_2)
#define BSP_I2C_SCL               (GPIO_NUM_3)

/* Display */
#define BSP_LCD_DATA0             (GPIO_NUM_8)
#define BSP_LCD_PCLK              (GPIO_NUM_9)
#define BSP_LCD_CS                (GPIO_NUM_NC)
#define BSP_LCD_DC                (GPIO_NUM_10)
#define BSP_LCD_RST               (GPIO_NUM_NC)
#define BSP_LCD_BACKLIGHT         (GPIO_NUM_24)

/* Light */
#define BSP_RGB_CTRL              (GPIO_NUM_0)
#define BSP_RGB_EXT_CTRL          (GPIO_NUM_26)

/* Audio */
#define BSP_ADC_MIC_CHANNEL       (4)
#define BSP_PDM_SPEAK_P_GPIO      (GPIO_NUM_27)
#define BSP_PDM_SPEAK_N_GPIO      (GPIO_NUM_4)
#define BSP_PA_CTL_GPIO           (GPIO_NUM_23)

#define BSP_INPUT_SAMPLE_RATE     (16000)
#define BSP_OUTPUT_SAMPLE_RATE    (16000)

#define BSP_AUDIO_VOLUME_MIN      (0)
#define BSP_AUDIO_VOLUME_MAX      (100)
#define BSP_AUDIO_VOLUME_DEFAULT  (100)

/* Touch */
#define BSP_TOUCH_INT             (GPIO_NUM_6)

/* Infrared Remote Control */
#define BSP_IR_TX_GPIO            (GPIO_NUM_25)    // MHL312IR039CRT connected to GPIO25
#define BSP_IR_CARRIER_FREQ       (38000)          // 38kHz carrier frequency
#define BSP_IR_RESOLUTION_HZ      (10 * 1000 * 1000)       // 1MHz resolution (1us tick)

/* Vibration Sensor */
#define BSP_VIBRATION_SENSOR_PIN  (GPIO_NUM_1)

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief BSP display configuration structure
 *
 */
typedef struct {
    lvgl_port_cfg_t lvgl_port_cfg;  /*!< LVGL port configuration */
    uint32_t        buffer_size;    /*!< Size of the buffer for the screen in pixels */
    bool            double_buffer;  /*!< True, if should be allocated two buffers */
    struct {
        unsigned int buff_dma: 1;    /*!< Allocated LVGL buffer will be DMA capable */
        unsigned int buff_spiram: 1; /*!< Allocated LVGL buffer will be in PSRAM */
    } flags;
} bsp_display_cfg_t;

/**************************************************************************************************
 *
 * LCD interface
 *
 * ESP32-C5-Sensairpanel is shipped with 1.28inch GC9A01 display controller.
 * It features 16-bit colors, 240x240 resolution.
 *
 * LVGL is used as graphics library. LVGL is NOT thread safe, therefore the user must take LVGL mutex
 * by calling bsp_display_lock() before calling and LVGL API (lv_...) and then give the mutex with
 * bsp_display_unlock().
 *
 * Display's backlight must be enabled explicitly by calling bsp_display_backlight_on()
 **************************************************************************************************/
#define BSP_LCD_PIXEL_CLOCK_HZ     (80 * 1000 * 1000)
#define BSP_LCD_SPI_NUM            (SPI2_HOST)

/**
 * @brief Initialize power control
 *
 * @return
 *     - ESP_OK Success
 *     - ESP_ERR_INVALID_ARG Parameter error
 */
void __attribute__((constructor)) bsp_power_control_init(void);
/**
 * @brief Control board power rail (active-high)
 */
void bsp_power_control_set_power(bool power_on);

/**
 * @brief Initialize display
 *
 * This function initializes SPI, display controller and starts LVGL handling task.
 * LCD backlight must be enabled separately by calling bsp_display_brightness_set()
 *
 * @return Pointer to LVGL display or NULL when error occurred
 */
lv_display_t *bsp_display_start(void);

/**
 * @brief Initialize display
 *
 * This function initializes SPI, display controller and starts LVGL handling task.
 * LCD backlight must be enabled separately by calling bsp_display_brightness_set()
 *
 * @param cfg display configuration
 *
 * @return Pointer to LVGL display or NULL when error occurred
 */
lv_disp_t *bsp_display_start_with_config(const bsp_display_cfg_t *cfg);


/**
 * @brief Take LVGL mutex
 *
 * @param timeout_ms Timeout in [ms]. 0 will block indefinitely.
 * @return true  Mutex was taken
 * @return false Mutex was NOT taken
 */
bool bsp_display_lock(uint32_t timeout_ms);

/**
 * @brief Give LVGL mutex
 *
 */
void bsp_display_unlock(void);

/**
 * @brief Rotate screen
 *
 * Display must be already initialized by calling bsp_display_start()
 *
 * @param[in] disp Pointer to LVGL display
 * @param[in] rotation Angle of the display rotation
 */
void bsp_display_rotate(lv_display_t *disp, lv_disp_rotation_t rotation);

/**************************************************************************************************
 *
 * WS2812
 *
 * There are 6 RGB LEDs on ESP32-C5-Sensairpanel arranged in a circle:
 * Each LED corresponds to a touch button in the same position
 * 
 * LED Layout (matches Touch Button Layout):
 *          4 (Top Left)    3 (Top Right)
 *                     \   /
 *                      \ /
 *       5 (Left) -------o------- 2 (Right)
 *                      / \
 *                     /   \
 *          6 (Bottom Left) 1 (Bottom Right)
 * 
 * LED-Touch Button Mapping:
 * - LED 0 -> Touch Button 1 (Bottom Right)
 * - LED 1 -> Touch Button 2 (Right)
 * - LED 2 -> Touch Button 3 (Top Right)
 * - LED 3 -> Touch Button 4 (Top Left)
 * - LED 4 -> Touch Button 5 (Left)
 * - LED 5 -> Touch Button 6 (Bottom Left)
 **************************************************************************************************/

#define BSP_LED_STRIP_COUNT     (6)     /*!< Total number of LEDs in the strip */
#define BSP_LED_ALL_INDEX       (0xFF)  /*!< Special index to control all LEDs */

typedef enum {
    BSP_LED_INDEX_0 = 0,    /*!< LED index 0 - Bottom Right (corresponds to Touch Button 1) */
    BSP_LED_INDEX_1,        /*!< LED index 1 - Right (corresponds to Touch Button 2) */
    BSP_LED_INDEX_2,        /*!< LED index 2 - Top Right (corresponds to Touch Button 3) */
    BSP_LED_INDEX_3,        /*!< LED index 3 - Top Left (corresponds to Touch Button 4) */
    BSP_LED_INDEX_4,        /*!< LED index 4 - Left (corresponds to Touch Button 5) */
    BSP_LED_INDEX_5,        /*!< LED index 5 - Bottom Left (corresponds to Touch Button 6) */
    BSP_LED_INDEX_MAX = BSP_LED_STRIP_COUNT
} bsp_led_index_t;

// Positional aliases for LEDs (matching touch button positions)
#define BSP_LED_BOTTOM_RIGHT    BSP_LED_INDEX_5  /*!< LED Bottom Right (Touch Button 1) */
#define BSP_LED_RIGHT           BSP_LED_INDEX_4  /*!< LED Right (Touch Button 2) */
#define BSP_LED_TOP_RIGHT       BSP_LED_INDEX_3  /*!< LED Top Right (Touch Button 3) */
#define BSP_LED_TOP_LEFT        BSP_LED_INDEX_2  /*!< LED Top Left (Touch Button 4) */
#define BSP_LED_LEFT            BSP_LED_INDEX_1  /*!< LED Left (Touch Button 5) */
#define BSP_LED_BOTTOM_LEFT     BSP_LED_INDEX_0  /*!< LED Bottom Left (Touch Button 6) */

typedef enum {
    BSP_LED_COLOR_OFF     = 0x000000,   /*!< LED off (black) */
    BSP_LED_COLOR_RED     = 0xFF0000,   /*!< Pure red */
    BSP_LED_COLOR_GREEN   = 0x00FF00,   /*!< Pure green */
    BSP_LED_COLOR_BLUE    = 0x0000FF,   /*!< Pure blue */
    BSP_LED_COLOR_WHITE   = 0xFFFFFF,   /*!< Pure white */
    BSP_LED_COLOR_YELLOW  = 0xFFFF00,   /*!< Yellow */
    BSP_LED_COLOR_CYAN    = 0x00FFFF,   /*!< Cyan */
    BSP_LED_COLOR_MAGENTA = 0xFF00FF,   /*!< Magenta */
    BSP_LED_COLOR_ORANGE  = 0xFF8000,   /*!< Orange */
    BSP_LED_COLOR_PURPLE  = 0x800080,   /*!< Purple */
} bsp_led_color_t;

typedef enum {
    BSP_LED_EFFECT_STATIC,       /*!< Static color */
    BSP_LED_EFFECT_BREATHING,    /*!< Breathing effect */
    BSP_LED_EFFECT_RAINBOW,      /*!< Rainbow effect */
    BSP_LED_EFFECT_CHASE,        /*!< Chase effect */
    BSP_LED_EFFECT_BLINK,        /*!< Blink effect */
    BSP_LED_EFFECT_FADE,         /*!< Fade effect */
    BSP_LED_EFFECT_MAX
} bsp_led_effect_t;

typedef enum {
    BSP_LED_BRIGHTNESS_OFF = 0,     /*!< 0% brightness */
    BSP_LED_BRIGHTNESS_LOW = 64,    /*!< 25% brightness */
    BSP_LED_BRIGHTNESS_MID = 128,   /*!< 50% brightness */
    BSP_LED_BRIGHTNESS_HIGH = 192,  /*!< 75% brightness */
    BSP_LED_BRIGHTNESS_MAX = 255,   /*!< 100% brightness */
} bsp_led_brightness_t;

typedef struct {
    uint32_t color;           /*!< RGB color (0xRRGGBB format) */
    uint8_t brightness;       /*!< Brightness (0-255) */
    bsp_led_effect_t effect;  /*!< Effect mode */
    uint16_t effect_speed;    /*!< Effect speed (ms) */
} bsp_led_config_t;

/**
 * @brief Initialize WS2812 LED strip
 *
 * @return
 *     - ESP_OK Success
 *     - ESP_ERR_INVALID_ARG Parameter error
 *     - ESP_FAIL Initialize failed
 */
esp_err_t bsp_led_init();

/**
 * @brief Deinitialize LED strip
 *
 * @return
 *     - ESP_OK Success  
 *     - ESP_FAIL Deinitialize failed
 */
esp_err_t bsp_led_deinit();

/**
 * @brief Set RGB color for a specific LED
 *
 * @param index LED index (0-5) or BSP_LED_ALL_INDEX for all LEDs
 * @param r Red component (0-255)
 * @param g Green component (0-255) 
 * @param b Blue component (0-255)
 *
 * @return
 *      - ESP_OK Success
 *      - ESP_ERR_INVALID_ARG Invalid LED index or color value
 *      - ESP_FAIL Set color failed
 */
esp_err_t bsp_led_set_rgb(uint8_t index, uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Set color for a specific LED using 32-bit RGB value
 *
 * @param index LED index (0-5) or BSP_LED_ALL_INDEX for all LEDs
 * @param color RGB color in 0xRRGGBB format
 *
 * @return
 *      - ESP_OK Success
 *      - ESP_ERR_INVALID_ARG Invalid LED index
 *      - ESP_FAIL Set color failed
 */
esp_err_t bsp_led_set_color(uint8_t index, uint32_t color);

/**
 * @brief Set HSV color for a specific LED
 *
 * @param index LED index (0-5) or BSP_LED_ALL_INDEX for all LEDs
 * @param hue Hue (0-360)
 * @param saturation Saturation (0-255)
 * @param value Value/brightness (0-255)
 *
 * @return
 *      - ESP_OK Success
 *      - ESP_ERR_INVALID_ARG Invalid parameters
 *      - ESP_FAIL Set color failed
 */
esp_err_t bsp_led_set_hsv(uint8_t index, uint16_t hue, uint8_t saturation, uint8_t value);

/**
 * @brief Set RGB colors for all LEDs at once
 *
 * @param colors Array of RGB colors (length must be BSP_LED_STRIP_COUNT)
 *
 * @return
 *      - ESP_OK Success
 *      - ESP_ERR_INVALID_ARG Invalid parameter
 *      - ESP_FAIL Set colors failed
 */
esp_err_t bsp_led_set_colors(const uint32_t *colors);

/**
 * @brief Set same RGB color for all LEDs
 *
 * @param r Red component (0-255)
 * @param g Green component (0-255)
 * @param b Blue component (0-255)
 *
 * @return
 *      - ESP_OK Success
 *      - ESP_FAIL Set color failed
 */
esp_err_t bsp_led_set_all_rgb(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Turn off specific LED or all LEDs
 *
 * @param index LED index (0-5) or BSP_LED_ALL_INDEX for all LEDs
 *
 * @return
 *      - ESP_OK Success
 *      - ESP_ERR_INVALID_ARG Invalid LED index
 *      - ESP_FAIL Operation failed
 */
esp_err_t bsp_led_clear(uint8_t index);

/**
 * @brief Turn off all LEDs
 *
 * @return
 *      - ESP_OK Success
 *      - ESP_FAIL Operation failed
 */
esp_err_t bsp_led_clear_all();

/**
 * @brief Set brightness for specific LED
 *
 * @param index LED index (0-5) or BSP_LED_ALL_INDEX for all LEDs
 * @param brightness Brightness level (0-255)
 *
 * @return
 *      - ESP_OK Success
 *      - ESP_ERR_INVALID_ARG Invalid parameters
 *      - ESP_FAIL Set brightness failed
 */
esp_err_t bsp_led_set_brightness(uint8_t index, uint8_t brightness);

/**
 * @brief Start LED effect
 *
 * @param effect Effect type
 * @param color Base color for effect
 * @param speed Effect speed in milliseconds
 *
 * @return
 *      - ESP_OK Success
 *      - ESP_ERR_INVALID_ARG Invalid parameters
 *      - ESP_FAIL Start effect failed
 */
esp_err_t bsp_led_start_effect(bsp_led_effect_t effect, uint32_t color, uint16_t speed);

/**
 * @brief Stop LED effect
 *
 * @return
 *      - ESP_OK Success
 *      - ESP_FAIL Stop effect failed
 */
esp_err_t bsp_led_stop_effect();

/**
 * @brief Set RGB for LED strip (compatible with old interface)
 * @note This function sets the same color for all LEDs
 *
 * @param r Red component (0-255)
 * @param g Green component (0-255)
 * @param b Blue component (0-255)
 *
 * @return
 *      - ESP_OK Success
 *      - ESP_FAIL Set color failed
 */
esp_err_t bsp_led_rgb_set(uint8_t r, uint8_t g, uint8_t b);

/**************************************************************************************************
 *
 * LED Matrix (16x16 WS2812 on BSP_RGB_EXT_CTRL)
 *
 **************************************************************************************************/

#define BSP_LED_MATRIX_ROWS      (16)
#define BSP_LED_MATRIX_COLS      (16)

typedef enum {
    BSP_LED_MX_EFFECT_STATIC = 0,
    BSP_LED_MX_EFFECT_BREATH,
    BSP_LED_MX_EFFECT_RAINBOW,
    BSP_LED_MX_EFFECT_SCAN,
    BSP_LED_MX_EFFECT_MAX
} bsp_led_matrix_effect_t;

/** Init/Deinit */
esp_err_t bsp_led_matrix_init(void);
esp_err_t bsp_led_matrix_deinit(void);

/** Basic draw */
esp_err_t bsp_led_matrix_set_pixel(uint16_t x, uint16_t y, uint32_t rgb);
esp_err_t bsp_led_matrix_fill(uint32_t rgb);
esp_err_t bsp_led_matrix_clear(void);
esp_err_t bsp_led_matrix_refresh(void);

/** Brightness (0-255, applied at refresh time) */
esp_err_t bsp_led_matrix_set_brightness(uint8_t level);

/** Effects */
esp_err_t bsp_led_matrix_start_effect(bsp_led_matrix_effect_t effect, uint32_t color, uint16_t speed_ms);
esp_err_t bsp_led_matrix_stop_effect(void);

/**************************************************************************************************
 *
 * I2S audio interface
 *
 * There is one device connected to the I2S peripheral:
 *  - PDM for output path
 *
 * For speaker initialization use bsp_audio_codec_speaker_init() which is inside initialize I2S with bsp_audio_init().
 * After speaker initialization, use functions from esp_codec_dev for play audio.
 * Example audio play:
 * \code{.c}
 * esp_codec_dev_open(spk_codec_dev, &fs);
 * esp_codec_dev_write(spk_codec_dev, wav_bytes, bytes_read_from_spiffs);
 * esp_codec_dev_close(spk_codec_dev);
 * \endcode
 **************************************************************************************************/

/**
 * @brief Initialize speaker codec device
 *
 * @return Pointer to codec device handle or NULL when error occurred
 */
esp_codec_dev_handle_t bsp_audio_codec_speaker_init(void);

/**
 * @brief 
 *
 * @return Pointer to codec device handle or NULL when error occurred
 */
esp_codec_dev_handle_t bsp_audio_codec_microphone_init(void);

/**
 * @brief Initialize audio player
 * 
 * @return
 *      - ESP_OK Success
 *      - ESP_ERR_INVALID_ARG Invalid parameter
 *      - ESP_FAIL Initialize failed
 */
esp_err_t bsp_audio_player_init(void);

/**
 * @brief Deinitialize audio player
 * 
 * @return
 *      - ESP_OK Success
 *      - ESP_ERR_INVALID_ARG Invalid parameter
 *      - ESP_FAIL Deinitialize failed
 */
esp_err_t bsp_audio_player_deinit(void);

/**
 * @brief Initialize file iterator
 * 
 * @param path Path to the file
 * @param instance Pointer to the file iterator instance
 * 
 * @return
 *      - ESP_OK Success
 *      - ESP_ERR_INVALID_ARG Invalid parameter
 *      - ESP_FAIL Initialize failed
 */
esp_err_t bsp_audio_file_iterator_init(const char *path, file_iterator_instance_t **instance);

/**
 * @brief Play audio file
 * 
 * @param file_path Path to the audio file
 * 
 * @return
 *      - ESP_OK Success
 *      - ESP_ERR_INVALID_ARG Invalid parameter
 *      - ESP_FAIL Play failed
 */
esp_err_t bsp_audio_play_file(const char *file_path);

/**
 * @brief Play audio file by index
 * 
 * @param instance Pointer to the file iterator instance
 * @param index Index of the audio file
 * 
 * @return
 *      - ESP_OK Success
 *      - ESP_ERR_INVALID_ARG Invalid parameter
 *      - ESP_FAIL Play failed
 */
esp_err_t bsp_audio_play_index(file_iterator_instance_t *instance, int index);

/**
 * @brief Register audio player callback
 * 
 * @param cb Callback function
 * @param user_data User data
 * 
 * @return
 *      - ESP_OK Success
 *      - ESP_ERR_INVALID_ARG Invalid parameter
 *      - ESP_FAIL Register failed
 */
void bsp_audio_register_callback(audio_player_cb_t cb, void *user_data);

/**
 * @brief 
 * 
 * @param volume Volume level (0-100)
 * @return
 *      - ESP_OK Success
 *      - ESP_ERR_INVALID_ARG Invalid parameter
 *      - ESP_FAIL Set volume failed
 */
esp_err_t bsp_audio_set_volume(uint8_t volume);

/**
 * @brief 
 * 
 * 
 * @param volume 
 * @return 
 */
esp_err_t bsp_audio_get_volume(uint8_t *volume);

esp_err_t bsp_wav_play_file(const char *file_path);
esp_err_t bsp_wav_play_file_async(const char *file_path);
esp_err_t bsp_wav_stop(void);
esp_err_t bsp_wav_init_async(void);
esp_err_t bsp_wav_deinit_async(void);

/**
 * @brief Read audio data directly from the microphone
 * 
 * This function provides direct access to read audio data from the microphone device.
 * It can be used to implement custom recording functionality with fine-grained control.
 * The microphone device will be automatically initialized and opened when first called.
 * 
 * @param buffer Pointer to audio data buffer to store read data
 * @param len Length of audio data to read in bytes
 * @param bytes_read Pointer to store actual bytes read
 * @param timeout_ms Timeout in milliseconds (currently unused but kept for compatibility)
 * 
 * @return
 *      - ESP_OK Success
 *      - ESP_ERR_INVALID_ARG Invalid parameter (buffer or bytes_read is NULL)
 *      - ESP_ERR_INVALID_STATE Microphone device initialization failed
 *      - ESP_FAIL Read operation failed
 */
esp_err_t bsp_audio_read(void *buffer, size_t len, size_t *bytes_read, uint32_t timeout_ms);

/**************************************************************************************************
 *
 * SPIFFS
 *
 * After mounting the SPIFFS, it can be accessed with stdio functions ie.:
 * \code{.c}
 * FILE* f = fopen(BSP_SPIFFS_MOUNT_POINT"/hello.txt", "w");
 * fprintf(f, "Hello World!\n");
 * fclose(f);
 * \endcode
 **************************************************************************************************/
#define BSP_SPIFFS_MOUNT_POINT      CONFIG_BSP_SPIFFS_MOUNT_POINT

/**
 * @brief Mount SPIFFS to virtual file system
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_STATE if esp_vfs_spiffs_register was already called
 *      - ESP_ERR_NO_MEM if memory can not be allocated
 *      - ESP_FAIL if partition can not be mounted
 *      - other error codes
 */
esp_err_t bsp_spiffs_mount(void);

/**
 * @brief Unmount SPIFFS from virtual file system
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_NOT_FOUND if the partition table does not contain SPIFFS partition with given label
 *      - ESP_ERR_INVALID_STATE if esp_vfs_spiffs_unregister was already called
 *      - ESP_ERR_NO_MEM if memory can not be allocated
 *      - ESP_FAIL if partition can not be mounted
 *      - other error codes
 */
esp_err_t bsp_spiffs_unmount(void);

/**************************************************************************************************
 *
 * Deep Sleep & Wake (Touch-based)
 *
 **************************************************************************************************/

/**
 * @brief Check if the system woke up due to GPIO (touch INT) wakeup
 *
 * @return true if wakeup cause is GPIO, false otherwise
 */
bool bsp_woke_from_touch(void);

/**
 * @brief Clear touch IC latched IRQ after wake (read status once)
 *
 * This performs a minimal touch IC init (no ISR), reads status to clear the latched
 * interrupt level, then deinitializes the touch IC to avoid duplication with later init.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t bsp_touch_wakeup_clear_irq(void);

/**
 * @brief Sleep preparation configuration
 */
typedef struct {
    bool turn_off_backlight;         /*!< Turn LCD backlight off before sleep */
    bool clear_leds;                 /*!< Clear ring LEDs and stop effects */
    bool stop_audio;                 /*!< Stop audio playback before sleep */
    bool deinit_buttons;             /*!< Deinitialize unified button system */
    bool deinit_i2c;                 /*!< Deinitialize I2C bus (optional) */
    bool power_off_before_sleep;     /*!< Pull BSP_POWER_CTRL low before deep sleep */
    esp_deepsleep_gpio_wake_up_mode_t  wake_level; /*!< Wakeup level for touch INT */
    bool use_internal_pullup;        /*!< Enable internal pull-up on INT if needed */
    bool wait_touch_release;         /*!< Wait INT release before entering sleep */
    uint32_t wait_timeout_ms;        /*!< Timeout for release wait */
} bsp_sleep_prep_cfg_t;

/**
 * @brief Quiesce board peripherals according to cfg
 */
esp_err_t bsp_sleep_quiesce(const bsp_sleep_prep_cfg_t *cfg);

/**
 * @brief One-shot: quiesce -> enable touch wake -> (optional wait) -> deep sleep
 */
esp_err_t bsp_sleep_prepare_and_enter(const bsp_sleep_prep_cfg_t *cfg);

/**************************************************************************************************
 *
 * I2C Bus Management
 *
 **************************************************************************************************/

/**
 * @brief I2C configuration structure for BSP
 */

#define BSP_I2C_CLK_SPEED     (100000)

typedef struct {
    gpio_num_t sda_io_num;              /*!< I2C SDA GPIO number */
    gpio_num_t scl_io_num;              /*!< I2C SCL GPIO number */
    uint32_t clk_speed;                 /*!< I2C clock speed in Hz */
    bool enable_internal_pullup;       /*!< Enable internal pull-up resistors */
} bsp_i2c_config_t;

/**
 * @brief Initialize I2C bus
 * 
 * This function initializes the I2C master bus with the provided configuration.
 * The I2C bus can be shared by multiple devices including the touch IC.
 * 
 * @param config I2C configuration structure
 * @return
 *      - ESP_OK: I2C bus initialized successfully
 *      - ESP_ERR_INVALID_ARG: Invalid configuration parameters
 *      - ESP_ERR_NO_MEM: Memory allocation failed
 *      - ESP_FAIL: I2C bus initialization failed
 */
esp_err_t bsp_i2c_init(const bsp_i2c_config_t *config);

/**
 * @brief Get I2C bus handle
 * 
 * @return I2C master bus handle or NULL if not initialized
 */
i2c_master_bus_handle_t bsp_i2c_get_bus_handle(void);

/**
 * @brief Deinitialize I2C bus
 * 
 * @return
 *      - ESP_OK: I2C bus deinitialized successfully
 *      - ESP_FAIL: Deinitialization failed
 */
esp_err_t bsp_i2c_deinit(void);

/**************************************************************************************************
 *
 * Unified Button Management System
 *
 **************************************************************************************************/

/**
 * @brief Touch Button Layout (Circular arrangement)
 * 
 *          4 (Top Left)    3 (Top Right)
 *                     \   /
 *                      \ /
 *       5 (Left) -------o------- 2 (Right)
 *                      / \
 *                     /   \
 *          6 (Bottom Left) 1 (Bottom Right)
 * 
 * Physical button mapping:
 * - Button 1: Bottom Right position
 * - Button 2: Right position  
 * - Button 3: Top Right position
 * - Button 4: Top Left position
 * - Button 5: Left position
 * - Button 6: Bottom Left position
 */

typedef enum {
    BSP_INPUT_BUTTON_USER = 0,    
    BSP_INPUT_TOUCH_1,            /*!< Touch Button 1 - Bottom Right */
    BSP_INPUT_TOUCH_2,            /*!< Touch Button 2 - Right */
    BSP_INPUT_TOUCH_3,            /*!< Touch Button 3 - Top Right */
    BSP_INPUT_TOUCH_4,            /*!< Touch Button 4 - Top Left */
    BSP_INPUT_TOUCH_5,            /*!< Touch Button 5 - Left */
    BSP_INPUT_TOUCH_6,            /*!< Touch Button 6 - Bottom Left */                    
    BSP_INPUT_MAX
} bsp_button_source_t;

// Positional aliases for better code readability
#define BSP_INPUT_TOUCH_BOTTOM_RIGHT    BSP_INPUT_TOUCH_1  /*!< Touch Button Bottom Right */
#define BSP_INPUT_TOUCH_RIGHT           BSP_INPUT_TOUCH_2  /*!< Touch Button Right */
#define BSP_INPUT_TOUCH_TOP_RIGHT       BSP_INPUT_TOUCH_3  /*!< Touch Button Top Right */
#define BSP_INPUT_TOUCH_TOP_LEFT        BSP_INPUT_TOUCH_4  /*!< Touch Button Top Left */
#define BSP_INPUT_TOUCH_LEFT            BSP_INPUT_TOUCH_5  /*!< Touch Button Left */
#define BSP_INPUT_TOUCH_BOTTOM_LEFT     BSP_INPUT_TOUCH_6  /*!< Touch Button Bottom Left */

typedef enum {
    BSP_BUTTON_EVENT_PRESS_DOWN = 0,
    BSP_BUTTON_EVENT_PRESS_UP,
    BSP_BUTTON_EVENT_SHORT_PRESS,
    BSP_BUTTON_EVENT_LONG_PRESS,
    BSP_BUTTON_EVENT_DOUBLE_CLICK,
    BSP_BUTTON_EVENT_MAX
} bsp_button_event_t;

typedef void (*bsp_button_callback_t)(bsp_button_source_t source, bsp_button_event_t event, void *user_data);

/**
 * @brief Initialize unified button management system
 * 
 * This function provides a one-stop initialization for all button input sources:
 * - Automatically initializes I2C bus for touch IC communication
 * - Configures and registers all 6 touch buttons (BS8112A3)
 * - Sets up unified event processing for all button types
 * - Prepares the system for additional button sources (GPIO buttons, etc.)
 * 
 * @param callback Global button event callback function for all button sources
 * @return
 *      - ESP_OK: Button system initialized successfully
 *      - ESP_ERR_INVALID_ARG: Invalid callback parameter
 *      - ESP_FAIL: Initialization failed (check logs for details)
 * 
 * @note This function handles all the complexity of initializing multiple button
 *       subsystems. After calling this function, all touch buttons are ready to use.
 * 
 */
esp_err_t bsp_button_init(bsp_button_callback_t callback);

/**
 * @brief Register additional button source
 * 
 * Use this function to add custom button sources beyond the built-in touch buttons.
 * The built-in touch buttons (BSP_INPUT_TOUCH_1 to BSP_INPUT_TOUCH_6) are 
 * automatically registered by bsp_button_init().
 * 
 * @param source Button source type (should not conflict with built-in sources)
 * @param config Button configuration structure
 * @return
 *      - ESP_OK: Button source registered successfully
 *      - ESP_ERR_INVALID_ARG: Invalid arguments or source already registered
 *      - ESP_FAIL: Registration failed
 */
esp_err_t bsp_button_register_source(bsp_button_source_t source, const button_config_t *config);

/**
 * @brief Set callback for specific button source and event (Advanced)
 * 
 * This is an advanced function for fine-grained control. Most applications
 * should use the global callback set in bsp_button_init() instead.
 * 
 * @param source Button source type
 * @param event Button event type
 * @param cb Callback function
 * @param user_data User data passed to callback
 * @return
 *      - ESP_OK: Callback set successfully
 *      - ESP_ERR_INVALID_ARG: Invalid arguments
 *      - ESP_FAIL: Setting callback failed
 */
esp_err_t bsp_button_set_callback(bsp_button_source_t source, bsp_button_event_t event, bsp_button_callback_t cb, void *user_data);

/**
 * @brief Deinitialize unified button management system
 * 
 * @return
 *      - ESP_OK: Button system deinitialized successfully
 *      - ESP_FAIL: Deinitialization failed
 */
esp_err_t bsp_button_deinit(void);

/**
 * @brief Check if touch hardware is available and functional
 * 
 * This function can be used to determine if touch buttons are available
 * before attempting to use touch-specific features.
 * 
 * @return
 *      - true: Touch hardware is available and functional
 *      - false: Touch hardware is not available or failed initialization
 */
bool bsp_touch_hardware_available(void);

/**
 * @brief Set LED color for a specific touch button position
 * 
 * @param touch_source Touch button source (BSP_INPUT_TOUCH_1 to BSP_INPUT_TOUCH_6)
 * @param color RGB color in 0xRRGGBB format
 * @return
 *      - ESP_OK Success
 *      - ESP_ERR_INVALID_ARG Invalid touch source
 *      - ESP_FAIL Set LED color failed
 */
esp_err_t bsp_led_set_for_touch(bsp_button_source_t touch_source, uint32_t color);

/**
 * @brief Set LED RGB color for a specific touch button position
 * 
 * @param touch_source Touch button source (BSP_INPUT_TOUCH_1 to BSP_INPUT_TOUCH_6) 
 * @param r Red component (0-255)
 * @param g Green component (0-255)
 * @param b Blue component (0-255)
 * @return
 *      - ESP_OK Success
 *      - ESP_ERR_INVALID_ARG Invalid touch source
 *      - ESP_FAIL Set LED color failed
 */
esp_err_t bsp_led_set_rgb_for_touch(bsp_button_source_t touch_source, uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Turn off LED for a specific touch button position
 * 
 * @param touch_source Touch button source (BSP_INPUT_TOUCH_1 to BSP_INPUT_TOUCH_6)
 * @return
 *      - ESP_OK Success
 *      - ESP_ERR_INVALID_ARG Invalid touch source
 *      - ESP_FAIL Operation failed
 */
esp_err_t bsp_led_clear_for_touch(bsp_button_source_t touch_source);

/**************************************************************************************************
 *
 * Light Sensor (RPR-0521) interface
 *
 * The ESP32-C5-Sensairpanel can optionally support RPR-0521 ambient light and proximity sensor
 * connected via I2C bus. The sensor provides ambient light data (DATA0 and DATA1) and proximity
 * detection capabilities.
 *
 **************************************************************************************************/

/* Light Sensor I2C Address */
#define BSP_LIGHT_SENSOR_I2C_ADDR     (0x38)

/**
 * @brief Initialize light sensor
 * 
 * This function initializes the RPR-0521 ambient light sensor.
 * The I2C bus must be initialized before calling this function.
 * If the sensor is not present, the BSP will continue to function
 * without light sensor capabilities.
 * 
 * @return
 *      - ESP_OK: Light sensor initialized successfully
 *      - ESP_ERR_INVALID_STATE: I2C bus not initialized
 *      - ESP_ERR_NOT_FOUND: Light sensor not found
 *      - ESP_FAIL: Initialization failed
 */
esp_err_t bsp_light_sensor_init(void);

/**
 * @brief Read ambient light DATA0 value
 * 
 * @param light_value Pointer to store ambient light DATA0 value
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: Invalid arguments
 *      - ESP_ERR_INVALID_STATE: Light sensor not initialized
 *      - ESP_FAIL: Read operation failed
 */
esp_err_t bsp_light_sensor_read_data0(uint16_t *light_value);

/**
 * @brief Read ambient light DATA1 value
 * 
 * @param light_value Pointer to store ambient light DATA1 value
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: Invalid arguments
 *      - ESP_ERR_INVALID_STATE: Light sensor not initialized
 *      - ESP_FAIL: Read operation failed
 */
esp_err_t bsp_light_sensor_read_data1(uint16_t *light_value);

/**
 * @brief Read proximity sensor value
 * 
 * @param proximity_value Pointer to store proximity sensor value
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: Invalid arguments
 *      - ESP_ERR_INVALID_STATE: Light sensor not initialized
 *      - ESP_FAIL: Read operation failed
 */
esp_err_t bsp_light_sensor_read_proximity(uint16_t *proximity_value);

/**
 * @brief Set light sensor gain for ambient light measurements
 * 
 * @param gain_level Gain level (0-3: 1x, 2x, 64x, 128x) for DATA0 and DATA1
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: Invalid gain level
 *      - ESP_ERR_INVALID_STATE: Light sensor not initialized
 *      - ESP_FAIL: Configuration failed
 */
esp_err_t bsp_light_sensor_set_gain(uint8_t gain_level);

/**
 * @brief Enable or disable proximity sensor
 * 
 * @param enable true to enable proximity sensor, false to disable
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_STATE: Light sensor not initialized
 *      - ESP_FAIL: Configuration failed
 */
esp_err_t bsp_light_sensor_enable_proximity(bool enable);

/**
 * @brief Deinitialize light sensor
 * 
 * @return
 *      - ESP_OK: Success
 *      - ESP_FAIL: Deinitialization failed
 */
esp_err_t bsp_light_sensor_deinit(void);

/**
 * @brief Check if light sensor is available and functional
 * 
 * @return
 *      - true: Light sensor is available and functional
 *      - false: Light sensor is not available
 */
bool bsp_light_sensor_available(void);

/**************************************************************************************************
 *
 * Infrared Remote Control Interface (simplified)
 *
 * BSP Only handles IR hardware initialization and some high-frequency convenient functions;
 * full capabilities should be used directly.
 **************************************************************************************************/

/**
 * @brief Initialize/Deinitialize/Status
 */
esp_err_t bsp_ir_init(void);
esp_err_t bsp_ir_deinit(void);
bool bsp_ir_is_initialized(void);

/**
 * @brief AC Quick Control
 */
esp_err_t bsp_ir_ac_quick_on(const char *brand, const char *model);
esp_err_t bsp_ir_ac_quick_off(const char *brand, const char *model);
esp_err_t bsp_ir_ac_set_temp(const char *brand, const char *model, uint8_t temperature);

/**
 * @brief TV Quick Control
 */
esp_err_t bsp_ir_tv_key(const char *brand, const char *model, ir_tv_keycode_t key);
esp_err_t bsp_ir_tv_digit(const char *brand, const char *model, uint8_t digit);

/**
 * @brief Send raw timing sequence (Advanced usage)
 */
esp_err_t bsp_ir_send_timing_us(const uint32_t *timing_pairs_us, size_t length);

/**************************************************************************************************
 *
 * Vibration Sensor interface
 *
 **************************************************************************************************/
/**
 * @brief Initialize vibration sensor
 * 
 * This function initializes the vibration sensor GPIO pin as input with pull-up enabled.
 * The sensor outputs low level when vibration is detected, high level when no vibration.
 * 
 * @return
 *      - ESP_OK: Vibration sensor initialized successfully
 *      - ESP_ERR_INVALID_ARG: Invalid GPIO configuration
 *      - ESP_FAIL: Initialization failed
 */
esp_err_t bsp_vibration_sensor_init(void);

/**
 * @brief Read vibration sensor status
 * 
 * This function reads the current state of the vibration sensor.
 * When vibration is detected, the pin outputs low level (0).
 * When no vibration is detected, the pin outputs high level (1).
 * 
 * @return true if vibration is detected, false if no vibration
 */
bool bsp_vibration_sensor_read(void);

/**
 * @brief Set vibration sensor sensitivity
 * 
 * This function sets the vibration detection sensitivity by adjusting
 * the threshold for level change count detection.
 * 
 * @param threshold Vibration detection threshold (1-50)
 *                  - Lower values: More sensitive, easier to trigger
 *                  - Higher values: More stable, fewer false triggers
 * @return
 *      - ESP_OK: Success
 *      - ESP_ERR_INVALID_ARG: Invalid threshold value
 */
esp_err_t bsp_vibration_sensor_set_sensitivity(int threshold);


#ifdef __cplusplus
}
#endif
