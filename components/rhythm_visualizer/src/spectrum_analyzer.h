#pragma once

#include "rhythm_visualizer.h"
#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>

esp_err_t spectrum_analyzer_init(void);
esp_err_t spectrum_analyzer_deinit(void);
esp_err_t spectrum_analyzer_process(const int16_t* audio_data, size_t samples, rhythm_spectrum_t* spectrum); 