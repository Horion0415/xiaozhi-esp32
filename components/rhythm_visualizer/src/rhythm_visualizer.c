#include "rhythm_visualizer.h"
#include "spectrum_analyzer.h"
#include "color_mapper.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>
#include <math.h>

static const char *TAG = "RHYTHM_VISUALIZER";

typedef struct {
    int16_t* data;
    size_t samples;
} audio_frame_t;

static struct {
    bool initialized;
    bool running;
    rhythm_scene_t current_scene;
    rhythm_effect_config_t config;
    
    TaskHandle_t render_task;
    QueueHandle_t audio_queue;
    esp_timer_handle_t frame_timer;
    
    uint32_t* color_buffer;
    rhythm_spectrum_t last_spectrum;
    
    char* audio_file_path;
    bool is_audio_file_mode;
    
    uint16_t matrix_rows;
    uint16_t matrix_cols;
    rhythm_hardware_interface_t hw_interface;
    void* user_data;
} s_rhythm_ctx = {0};

static void render_task_function(void* arg);
static void frame_timer_callback(void* arg);

esp_err_t rhythm_visualizer_init(const rhythm_config_t* config) {
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (s_rhythm_ctx.initialized) {
        return ESP_OK;
    }
    
    memset(&s_rhythm_ctx, 0, sizeof(s_rhythm_ctx));
    
    s_rhythm_ctx.matrix_rows = config->matrix_rows;
    s_rhythm_ctx.matrix_cols = config->matrix_cols;
    s_rhythm_ctx.hw_interface = config->hw_interface;
    s_rhythm_ctx.user_data = config->user_data;
    
    size_t matrix_pixels = s_rhythm_ctx.matrix_rows * s_rhythm_ctx.matrix_cols;
    s_rhythm_ctx.color_buffer = (uint32_t*)malloc(matrix_pixels * sizeof(uint32_t));
    if (!s_rhythm_ctx.color_buffer) {
        ESP_LOGE(TAG, "Failed to allocate color buffer");
        return ESP_ERR_NO_MEM;
    }
    
    esp_err_t ret = ESP_OK;
    if (s_rhythm_ctx.hw_interface.led_matrix_init) {
        ret = s_rhythm_ctx.hw_interface.led_matrix_init(s_rhythm_ctx.user_data);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "LED matrix init failed");
            free(s_rhythm_ctx.color_buffer);
            return ret;
        }
    }
    
    ret = spectrum_analyzer_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Spectrum analyzer init failed");
        return ret;
    }
    
    ret = color_mapper_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Color mapper init failed");
        return ret;
    }
    
    s_rhythm_ctx.audio_queue = xQueueCreate(8, sizeof(audio_frame_t));
    if (!s_rhythm_ctx.audio_queue) {
        ESP_LOGE(TAG, "Audio queue create failed");
        return ESP_ERR_NO_MEM;
    }
    
    s_rhythm_ctx.config.sensitivity = 5;
    s_rhythm_ctx.config.speed = 5;
    s_rhythm_ctx.config.brightness = 128;
    s_rhythm_ctx.config.smooth_mode = true;
    
    s_rhythm_ctx.current_scene = RHYTHM_SCENE_SPECTRUM;
    s_rhythm_ctx.initialized = true;
    
    ESP_LOGI(TAG, "Rhythm visualizer initialized");
    return ESP_OK;
}

esp_err_t rhythm_visualizer_deinit(void) {
    if (!s_rhythm_ctx.initialized) {
        return ESP_OK;
    }
    
    rhythm_stop();
    
    if (s_rhythm_ctx.audio_queue) {
        vQueueDelete(s_rhythm_ctx.audio_queue);
        s_rhythm_ctx.audio_queue = NULL;
    }
    
    if (s_rhythm_ctx.audio_file_path) {
        free(s_rhythm_ctx.audio_file_path);
        s_rhythm_ctx.audio_file_path = NULL;
    }
    
    color_mapper_deinit();
    spectrum_analyzer_deinit();
    
    if (s_rhythm_ctx.hw_interface.led_matrix_clear) {
        s_rhythm_ctx.hw_interface.led_matrix_clear(s_rhythm_ctx.user_data);
    }
    if (s_rhythm_ctx.hw_interface.led_matrix_refresh) {
        s_rhythm_ctx.hw_interface.led_matrix_refresh(s_rhythm_ctx.user_data);
    }
    if (s_rhythm_ctx.hw_interface.led_matrix_deinit) {
        s_rhythm_ctx.hw_interface.led_matrix_deinit(s_rhythm_ctx.user_data);
    }
    
    if (s_rhythm_ctx.color_buffer) {
        free(s_rhythm_ctx.color_buffer);
        s_rhythm_ctx.color_buffer = NULL;
    }
    
    s_rhythm_ctx.initialized = false;
    
    ESP_LOGI(TAG, "Rhythm visualizer deinitialized");
    return ESP_OK;
}

esp_err_t rhythm_start_natural_sound(rhythm_scene_t scene, const char* audio_file) {
    if (!s_rhythm_ctx.initialized || scene >= RHYTHM_SCENE_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    
    rhythm_stop();
    
    esp_err_t ret = ESP_OK;
    if (s_rhythm_ctx.hw_interface.audio_player_init) {
        ret = s_rhythm_ctx.hw_interface.audio_player_init(s_rhythm_ctx.user_data);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to init audio player");
            return ret;
        }
    }
    
    if (audio_file) {
        size_t len = strlen(audio_file) + 1;
        s_rhythm_ctx.audio_file_path = malloc(len);
        if (!s_rhythm_ctx.audio_file_path) {
            return ESP_ERR_NO_MEM;
        }
        strcpy(s_rhythm_ctx.audio_file_path, audio_file);
        s_rhythm_ctx.is_audio_file_mode = true;
        
        if (s_rhythm_ctx.hw_interface.audio_play_file) {
            ret = s_rhythm_ctx.hw_interface.audio_play_file(audio_file, s_rhythm_ctx.user_data);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to start audio playback");
                free(s_rhythm_ctx.audio_file_path);
                s_rhythm_ctx.audio_file_path = NULL;
                return ret;
            }
        }
    } else {
        s_rhythm_ctx.is_audio_file_mode = false;
    }
    
    s_rhythm_ctx.current_scene = scene;
    s_rhythm_ctx.running = true;
    
    if (xTaskCreate(render_task_function, "rhythm_render", 4096, NULL, 5, &s_rhythm_ctx.render_task) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create render task");
        s_rhythm_ctx.running = false;
        return ESP_ERR_NO_MEM;
    }
    
    esp_timer_create_args_t timer_args = {
        .callback = frame_timer_callback,
        .arg = NULL,
        .name = "rhythm_timer"
    };
    
    ret = esp_timer_create(&timer_args, &s_rhythm_ctx.frame_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create frame timer");
        rhythm_stop();
        return ret;
    }
    
    uint32_t period_ms = 1000 / (20 + s_rhythm_ctx.config.speed * 4);
    ret = esp_timer_start_periodic(s_rhythm_ctx.frame_timer, period_ms * 1000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start frame timer");
        rhythm_stop();
        return ret;
    }
    
    ESP_LOGI(TAG, "Rhythm visualizer started, scene=%d", scene);
    return ESP_OK;
}

esp_err_t rhythm_start_live_mode(rhythm_scene_t scene) {
    return rhythm_start_natural_sound(scene, NULL);
}

esp_err_t rhythm_stop(void) {
    if (!s_rhythm_ctx.running) {
        return ESP_OK;
    }
    
    s_rhythm_ctx.running = false;
    
    if (s_rhythm_ctx.frame_timer) {
        esp_timer_stop(s_rhythm_ctx.frame_timer);
        esp_timer_delete(s_rhythm_ctx.frame_timer);
        s_rhythm_ctx.frame_timer = NULL;
    }
    
    if (s_rhythm_ctx.render_task) {
        vTaskDelete(s_rhythm_ctx.render_task);
        s_rhythm_ctx.render_task = NULL;
    }
    
    if (s_rhythm_ctx.is_audio_file_mode && s_rhythm_ctx.hw_interface.audio_stop) {
        s_rhythm_ctx.hw_interface.audio_stop(s_rhythm_ctx.user_data);
    }
    
    if (s_rhythm_ctx.audio_file_path) {
        free(s_rhythm_ctx.audio_file_path);
        s_rhythm_ctx.audio_file_path = NULL;
    }
    
    if (s_rhythm_ctx.hw_interface.led_matrix_clear) {
        s_rhythm_ctx.hw_interface.led_matrix_clear(s_rhythm_ctx.user_data);
    }
    if (s_rhythm_ctx.hw_interface.led_matrix_refresh) {
        s_rhythm_ctx.hw_interface.led_matrix_refresh(s_rhythm_ctx.user_data);
    }
    
    ESP_LOGI(TAG, "Rhythm visualizer stopped");
    return ESP_OK;
}

esp_err_t rhythm_set_effect_config(const rhythm_effect_config_t* config) {
    if (!s_rhythm_ctx.initialized || !config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memcpy(&s_rhythm_ctx.config, config, sizeof(rhythm_effect_config_t));
    
    if (s_rhythm_ctx.running && s_rhythm_ctx.frame_timer) {
        esp_timer_stop(s_rhythm_ctx.frame_timer);
        uint32_t period_ms = 1000 / (20 + s_rhythm_ctx.config.speed * 4);
        esp_timer_start_periodic(s_rhythm_ctx.frame_timer, period_ms * 1000);
    }
    
    return ESP_OK;
}

esp_err_t rhythm_get_effect_config(rhythm_effect_config_t* config) {
    if (!s_rhythm_ctx.initialized || !config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memcpy(config, &s_rhythm_ctx.config, sizeof(rhythm_effect_config_t));
    return ESP_OK;
}

esp_err_t rhythm_process_audio_frame(const int16_t* audio_data, size_t samples) {
    if (!s_rhythm_ctx.initialized || !s_rhythm_ctx.running || !audio_data) {
        return ESP_ERR_INVALID_STATE;
    }
    
    audio_frame_t frame = {
        .data = (int16_t*)audio_data,
        .samples = samples
    };
    
    if (xQueueSend(s_rhythm_ctx.audio_queue, &frame, 0) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    return ESP_OK;
}

bool rhythm_is_running(void) {
    return s_rhythm_ctx.running;
}

rhythm_scene_t rhythm_get_current_scene(void) {
    return s_rhythm_ctx.current_scene;
}

static void render_task_function(void* arg) {
    audio_frame_t frame;
    rhythm_spectrum_t spectrum = {0};
    uint32_t tick_count = 0;
    
    ESP_LOGI(TAG, "Render task started");
    
    while (s_rhythm_ctx.running) {
        tick_count++;
        
        if (s_rhythm_ctx.is_audio_file_mode) {
            spectrum.low_energy = 0.3f + 0.4f * sinf(tick_count * 0.1f);
            spectrum.mid_energy = 0.2f + 0.3f * sinf(tick_count * 0.15f + 1.0f);
            spectrum.high_energy = 0.1f + 0.2f * sinf(tick_count * 0.2f + 2.0f);
            spectrum.total_energy = spectrum.low_energy + spectrum.mid_energy + spectrum.high_energy;
        } else {
            if (xQueueReceive(s_rhythm_ctx.audio_queue, &frame, pdMS_TO_TICKS(50)) == pdTRUE) {
                esp_err_t ret = spectrum_analyzer_process(frame.data, frame.samples, &spectrum);
                if (ret != ESP_OK) {
                    spectrum.low_energy = 0.1f;
                    spectrum.mid_energy = 0.1f;
                    spectrum.high_energy = 0.1f;
                    spectrum.total_energy = 0.3f;
                }
            } else {
                spectrum.low_energy = 0.05f;
                spectrum.mid_energy = 0.05f;
                spectrum.high_energy = 0.05f;
                spectrum.total_energy = 0.15f;
            }
        }
        
        if (s_rhythm_ctx.config.smooth_mode) {
            spectrum.low_energy = (s_rhythm_ctx.last_spectrum.low_energy * 0.7f) + (spectrum.low_energy * 0.3f);
            spectrum.mid_energy = (s_rhythm_ctx.last_spectrum.mid_energy * 0.7f) + (spectrum.mid_energy * 0.3f);
            spectrum.high_energy = (s_rhythm_ctx.last_spectrum.high_energy * 0.7f) + (spectrum.high_energy * 0.3f);
            spectrum.total_energy = spectrum.low_energy + spectrum.mid_energy + spectrum.high_energy;
        }
        
        memcpy(&s_rhythm_ctx.last_spectrum, &spectrum, sizeof(rhythm_spectrum_t));
        
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    
    ESP_LOGI(TAG, "Render task ended");
    vTaskDelete(NULL);
}

static void frame_timer_callback(void* arg) {
    if (!s_rhythm_ctx.running) {
        return;
    }
    
    esp_err_t ret = color_mapper_map_spectrum(&s_rhythm_ctx.last_spectrum, 
                                             s_rhythm_ctx.current_scene,
                                             &s_rhythm_ctx.config,
                                             s_rhythm_ctx.color_buffer,
                                             s_rhythm_ctx.matrix_rows,
                                             s_rhythm_ctx.matrix_cols);
    
    if (ret == ESP_OK && s_rhythm_ctx.hw_interface.led_matrix_set_pixel) {
        for (int y = 0; y < s_rhythm_ctx.matrix_rows; y++) {
            for (int x = 0; x < s_rhythm_ctx.matrix_cols; x++) {
                int idx = y * s_rhythm_ctx.matrix_cols + x;
                s_rhythm_ctx.hw_interface.led_matrix_set_pixel(x, y, s_rhythm_ctx.color_buffer[idx], s_rhythm_ctx.user_data);
            }
        }
        if (s_rhythm_ctx.hw_interface.led_matrix_refresh) {
            s_rhythm_ctx.hw_interface.led_matrix_refresh(s_rhythm_ctx.user_data);
        }
    }
} 