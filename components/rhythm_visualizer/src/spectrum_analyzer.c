#include "spectrum_analyzer.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

#define SAMPLE_RATE 16000
#define FFT_SIZE 256
#define FREQ_BANDS 3

typedef struct {
    float real[FFT_SIZE];
    float imag[FFT_SIZE];
    float magnitude[FFT_SIZE];
    float window[FFT_SIZE];
} fft_context_t;

static fft_context_t s_fft_ctx;
static bool s_initialized = false;

static void init_hamming_window(void) {
    for (int i = 0; i < FFT_SIZE; i++) {
        s_fft_ctx.window[i] = 0.54f - 0.46f * cosf(2.0f * M_PI * i / (FFT_SIZE - 1));
    }
}

static void simple_fft(float* real, float* imag, int n) {
    if (n <= 1) return;
    
    float* even_real = (float*)malloc(n/2 * sizeof(float));
    float* even_imag = (float*)malloc(n/2 * sizeof(float));
    float* odd_real = (float*)malloc(n/2 * sizeof(float));
    float* odd_imag = (float*)malloc(n/2 * sizeof(float));
    
    for (int i = 0; i < n/2; i++) {
        even_real[i] = real[i*2];
        even_imag[i] = imag[i*2];
        odd_real[i] = real[i*2+1];
        odd_imag[i] = imag[i*2+1];
    }
    
    simple_fft(even_real, even_imag, n/2);
    simple_fft(odd_real, odd_imag, n/2);
    
    for (int i = 0; i < n/2; i++) {
        float t_real = cosf(-2.0f * M_PI * i / n) * odd_real[i] - sinf(-2.0f * M_PI * i / n) * odd_imag[i];
        float t_imag = sinf(-2.0f * M_PI * i / n) * odd_real[i] + cosf(-2.0f * M_PI * i / n) * odd_imag[i];
        
        real[i] = even_real[i] + t_real;
        imag[i] = even_imag[i] + t_imag;
        real[i + n/2] = even_real[i] - t_real;
        imag[i + n/2] = even_imag[i] - t_imag;
    }
    
    free(even_real);
    free(even_imag);
    free(odd_real);
    free(odd_imag);
}

esp_err_t spectrum_analyzer_init(void) {
    if (s_initialized) {
        return ESP_OK;
    }
    
    memset(&s_fft_ctx, 0, sizeof(fft_context_t));
    init_hamming_window();
    s_initialized = true;
    
    return ESP_OK;
}

esp_err_t spectrum_analyzer_deinit(void) {
    s_initialized = false;
    return ESP_OK;
}

esp_err_t spectrum_analyzer_process(const int16_t* audio_data, size_t samples, rhythm_spectrum_t* spectrum) {
    if (!s_initialized || !audio_data || !spectrum || samples > FFT_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }
    
    for (size_t i = 0; i < samples && i < FFT_SIZE; i++) {
        s_fft_ctx.real[i] = (float)audio_data[i] * s_fft_ctx.window[i] / 32768.0f;
        s_fft_ctx.imag[i] = 0.0f;
    }
    
    for (size_t i = samples; i < FFT_SIZE; i++) {
        s_fft_ctx.real[i] = 0.0f;
        s_fft_ctx.imag[i] = 0.0f;
    }
    
    simple_fft(s_fft_ctx.real, s_fft_ctx.imag, FFT_SIZE);
    
    for (int i = 0; i < FFT_SIZE/2; i++) {
        s_fft_ctx.magnitude[i] = sqrtf(s_fft_ctx.real[i] * s_fft_ctx.real[i] + 
                                      s_fft_ctx.imag[i] * s_fft_ctx.imag[i]);
    }
    
    int low_end = (int)(250.0f * FFT_SIZE / SAMPLE_RATE);
    int mid_end = (int)(4000.0f * FFT_SIZE / SAMPLE_RATE);
    int high_end = FFT_SIZE / 2;
    
    spectrum->low_energy = 0.0f;
    spectrum->mid_energy = 0.0f;
    spectrum->high_energy = 0.0f;
    
    for (int i = 1; i < low_end; i++) {
        spectrum->low_energy += s_fft_ctx.magnitude[i];
    }
    
    for (int i = low_end; i < mid_end; i++) {
        spectrum->mid_energy += s_fft_ctx.magnitude[i];
    }
    
    for (int i = mid_end; i < high_end; i++) {
        spectrum->high_energy += s_fft_ctx.magnitude[i];
    }
    
    spectrum->low_energy /= (low_end - 1);
    spectrum->mid_energy /= (mid_end - low_end);
    spectrum->high_energy /= (high_end - mid_end);
    spectrum->total_energy = spectrum->low_energy + spectrum->mid_energy + spectrum->high_energy;
    
    return ESP_OK;
} 