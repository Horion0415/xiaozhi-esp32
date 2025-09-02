#include "color_mapper.h"
#include <math.h>
#include <string.h>

static bool s_initialized = false;
static uint32_t s_frame_counter = 0;
static uint16_t s_matrix_rows = 16;
static uint16_t s_matrix_cols = 16;
static uint32_t s_matrix_pixels = 256;

static uint32_t hsv_to_rgb(float h, float s, float v) {
    float c = v * s;
    float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;
    
    float r, g, b;
    if (h >= 0 && h < 60) {
        r = c; g = x; b = 0;
    } else if (h >= 60 && h < 120) {
        r = x; g = c; b = 0;
    } else if (h >= 120 && h < 180) {
        r = 0; g = c; b = x;
    } else if (h >= 180 && h < 240) {
        r = 0; g = x; b = c;
    } else if (h >= 240 && h < 300) {
        r = x; g = 0; b = c;
    } else {
        r = c; g = 0; b = x;
    }
    
    uint8_t red = (uint8_t)((r + m) * 255);
    uint8_t green = (uint8_t)((g + m) * 255);
    uint8_t blue = (uint8_t)((b + m) * 255);
    
    return (red << 16) | (green << 8) | blue;
}

static uint32_t interpolate_color(uint32_t color1, uint32_t color2, float ratio) {
    uint8_t r1 = (color1 >> 16) & 0xFF;
    uint8_t g1 = (color1 >> 8) & 0xFF;
    uint8_t b1 = color1 & 0xFF;
    
    uint8_t r2 = (color2 >> 16) & 0xFF;
    uint8_t g2 = (color2 >> 8) & 0xFF;
    uint8_t b2 = color2 & 0xFF;
    
    uint8_t r = (uint8_t)(r1 + (r2 - r1) * ratio);
    uint8_t g = (uint8_t)(g1 + (g2 - g1) * ratio);
    uint8_t b = (uint8_t)(b1 + (b2 - b1) * ratio);
    
    return (r << 16) | (g << 8) | b;
}

static void map_fire_scene(const rhythm_spectrum_t* spectrum, const rhythm_effect_config_t* config, uint32_t* colors) {
    float intensity = fminf(spectrum->low_energy * config->sensitivity * 0.1f, 1.0f);
    float flicker = sinf(s_frame_counter * 0.3f) * 0.2f + 0.8f;
    
    uint32_t base_colors[] = {0xFF4500, 0xFF6600, 0xFF8800, 0xFFAA00};
    uint32_t flame_color = interpolate_color(0x330000, base_colors[s_frame_counter % 4], intensity * flicker);
    
    for (int y = 0; y < s_matrix_rows; y++) {
        for (int x = 0; x < s_matrix_cols; x++) {
            int idx = y * s_matrix_cols + x;
            float distance = sqrtf(powf(x - s_matrix_cols/2, 2) + powf(y - s_matrix_rows*3/4, 2)) / (s_matrix_cols/2);
            float fade = fmaxf(0.0f, 1.0f - distance);
            
            uint8_t r = ((flame_color >> 16) & 0xFF) * fade * (config->brightness / 255.0f);
            uint8_t g = ((flame_color >> 8) & 0xFF) * fade * (config->brightness / 255.0f);
            uint8_t b = (flame_color & 0xFF) * fade * (config->brightness / 255.0f);
            
            colors[idx] = (r << 16) | (g << 8) | b;
        }
    }
}

static void map_rain_scene(const rhythm_spectrum_t* spectrum, const rhythm_effect_config_t* config, uint32_t* colors) {
    float intensity = fminf(spectrum->mid_energy * config->sensitivity * 0.1f, 1.0f);
    
    memset(colors, 0, s_matrix_pixels * sizeof(uint32_t));
    
    int drop_count = (int)(intensity * 8 + 2);
    for (int i = 0; i < drop_count; i++) {
        int x = (s_frame_counter * 7 + i * 13) % s_matrix_cols;
        int y_start = (s_frame_counter * config->speed / 5) % (s_matrix_rows + 8);
        
        for (int j = 0; j < 3; j++) {
            int y = y_start - j;
            if (y >= 0 && y < s_matrix_rows) {
                int idx = y * s_matrix_cols + x;
                float alpha = (3 - j) / 3.0f;
                uint8_t blue = (uint8_t)(255 * alpha * intensity * (config->brightness / 255.0f));
                colors[idx] = blue;
            }
        }
    }
}

static void map_wave_scene(const rhythm_spectrum_t* spectrum, const rhythm_effect_config_t* config, uint32_t* colors) {
    float intensity = fminf(spectrum->total_energy * config->sensitivity * 0.1f, 1.0f);
    
    for (int y = 0; y < s_matrix_rows; y++) {
        for (int x = 0; x < s_matrix_cols; x++) {
            int idx = y * s_matrix_cols + x;
            
            float wave = sinf((x * 0.5f + s_frame_counter * config->speed * 0.05f) * M_PI / (s_matrix_cols/2));
            float wave_y = s_matrix_rows/2 + wave * (s_matrix_rows/4) * intensity;
            
            float distance = fabsf(y - wave_y);
            float alpha = fmaxf(0.0f, 1.0f - distance / 3.0f);
            
            uint32_t wave_color = interpolate_color(0x004466, 0x0088CC, intensity);
            uint8_t r = ((wave_color >> 16) & 0xFF) * alpha * (config->brightness / 255.0f);
            uint8_t g = ((wave_color >> 8) & 0xFF) * alpha * (config->brightness / 255.0f);
            uint8_t b = (wave_color & 0xFF) * alpha * (config->brightness / 255.0f);
            
            colors[idx] = (r << 16) | (g << 8) | b;
        }
    }
}

static void map_spectrum_scene(const rhythm_spectrum_t* spectrum, const rhythm_effect_config_t* config, uint32_t* colors) {
    for (int x = 0; x < s_matrix_cols; x++) {
        float freq_ratio = (float)x / s_matrix_cols;
        float energy;
        
        if (freq_ratio < 0.33f) {
            energy = spectrum->low_energy;
        } else if (freq_ratio < 0.66f) {
            energy = spectrum->mid_energy;
        } else {
            energy = spectrum->high_energy;
        }
        
        int height = (int)(fminf(energy * config->sensitivity * 0.5f, 1.0f) * s_matrix_rows);
        float hue = freq_ratio * 300.0f;
        uint32_t color = hsv_to_rgb(hue, 1.0f, config->brightness / 255.0f);
        
        for (int y = 0; y < s_matrix_rows; y++) {
            int idx = y * s_matrix_cols + x;
            if (y >= s_matrix_rows - height) {
                colors[idx] = color;
            } else {
                colors[idx] = 0;
            }
        }
    }
}

esp_err_t color_mapper_init(void) {
    if (s_initialized) {
        return ESP_OK;
    }
    
    s_frame_counter = 0;
    s_initialized = true;
    return ESP_OK;
}

esp_err_t color_mapper_deinit(void) {
    s_initialized = false;
    return ESP_OK;
}

esp_err_t color_mapper_map_spectrum(const rhythm_spectrum_t* spectrum, rhythm_scene_t scene, 
                                   const rhythm_effect_config_t* config, uint32_t* colors,
                                   uint16_t matrix_rows, uint16_t matrix_cols) {
    if (!s_initialized || !spectrum || !config || !colors) {
        return ESP_ERR_INVALID_ARG;
    }
    
    s_matrix_rows = matrix_rows;
    s_matrix_cols = matrix_cols;
    s_matrix_pixels = matrix_rows * matrix_cols;
    
    s_frame_counter++;
    
    switch (scene) {
        case RHYTHM_SCENE_FIRE:
            map_fire_scene(spectrum, config, colors);
            break;
        case RHYTHM_SCENE_RAIN:
            map_rain_scene(spectrum, config, colors);
            break;
        case RHYTHM_SCENE_WAVE:
            map_wave_scene(spectrum, config, colors);
            break;
        case RHYTHM_SCENE_SPECTRUM:
            map_spectrum_scene(spectrum, config, colors);
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }
    
    return ESP_OK;
} 