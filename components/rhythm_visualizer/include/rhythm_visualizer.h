#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RHYTHM_SCENE_FIRE = 0,
    RHYTHM_SCENE_RAIN,
    RHYTHM_SCENE_WAVE,
    RHYTHM_SCENE_SPECTRUM,
    RHYTHM_SCENE_MAX
} rhythm_scene_t;

typedef struct {
    uint8_t sensitivity;
    uint8_t speed;
    uint8_t brightness;
    bool smooth_mode;
} rhythm_effect_config_t;

typedef struct {
    float low_energy;
    float mid_energy;
    float high_energy;
    float total_energy;
} rhythm_spectrum_t;

typedef struct {
    esp_err_t (*led_matrix_init)(void* user_data);
    esp_err_t (*led_matrix_set_pixel)(uint16_t x, uint16_t y, uint32_t color, void* user_data);
    esp_err_t (*led_matrix_clear)(void* user_data);
    esp_err_t (*led_matrix_refresh)(void* user_data);
    esp_err_t (*led_matrix_deinit)(void* user_data);
    
    esp_err_t (*audio_player_init)(void* user_data);
    esp_err_t (*audio_play_file)(const char* file_path, void* user_data);
    esp_err_t (*audio_stop)(void* user_data);
    esp_err_t (*audio_player_deinit)(void* user_data);
    
    esp_err_t (*audio_read)(void* buffer, size_t len, size_t* bytes_read, uint32_t timeout_ms, void* user_data);
} rhythm_hardware_interface_t;

typedef struct {
    uint16_t matrix_rows;
    uint16_t matrix_cols;
    rhythm_hardware_interface_t hw_interface;
    void* user_data;
} rhythm_config_t;

esp_err_t rhythm_visualizer_init(const rhythm_config_t* config);
esp_err_t rhythm_visualizer_deinit(void);

esp_err_t rhythm_start_natural_sound(rhythm_scene_t scene, const char* audio_file);
esp_err_t rhythm_start_live_mode(rhythm_scene_t scene);
esp_err_t rhythm_stop(void);

esp_err_t rhythm_set_effect_config(const rhythm_effect_config_t* config);
esp_err_t rhythm_get_effect_config(rhythm_effect_config_t* config);

esp_err_t rhythm_process_audio_frame(const int16_t* audio_data, size_t samples);

bool rhythm_is_running(void);
rhythm_scene_t rhythm_get_current_scene(void);

#ifdef __cplusplus
}
#endif 