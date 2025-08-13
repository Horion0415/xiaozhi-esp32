#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>
#include <driver/spi_master.h>

// Audio
#define AUDIO_INPUT_SAMPLE_RATE   16000
#define AUDIO_OUTPUT_SAMPLE_RATE  16000
#define AUDIO_INPUT_REFERENCE     false

#define AUDIO_ADC_CHANNEL         4
#define AUDIO_PDM_SPEAK_P         GPIO_NUM_27
#define AUDIO_PDM_SPEAK_N         GPIO_NUM_4
#define AUDIO_PA_CTRL             GPIO_NUM_23

// Display GC9A01 240x240
#define DISPLAY_WIDTH       240
#define DISPLAY_HEIGHT      240
#define DISPLAY_MIRROR_X    true
#define DISPLAY_MIRROR_Y    false
#define DISPLAY_SWAP_XY     false
#define DISPLAY_OFFSET_X    0
#define DISPLAY_OFFSET_Y    0

#define DISPLAY_SPI_HOST    SPI2_HOST
#define DISPLAY_DATA0_PIN   GPIO_NUM_8
#define DISPLAY_PCLK_PIN    GPIO_NUM_9
#define DISPLAY_DC_PIN      GPIO_NUM_10
#define DISPLAY_RST_PIN     GPIO_NUM_NC
#define DISPLAY_BL_PIN      GPIO_NUM_24

// LED ring
#define LED_RING_GPIO       GPIO_NUM_0
#define LED_RING_COUNT      6

// Power rail
#define POWER_CTRL_GPIO     GPIO_NUM_7

#endif // _BOARD_CONFIG_H_ 