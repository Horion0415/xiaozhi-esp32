#pragma once

#include "rhythm_visualizer.h"
#include "esp_err.h"
#include <stdint.h>

esp_err_t color_mapper_init(void);
esp_err_t color_mapper_deinit(void);
esp_err_t color_mapper_map_spectrum(const rhythm_spectrum_t* spectrum, rhythm_scene_t scene, 
                                   const rhythm_effect_config_t* config, uint32_t* colors,
                                   uint16_t matrix_rows, uint16_t matrix_cols); 