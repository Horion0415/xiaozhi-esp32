#include "boards/esp-hi/adc_pdm_audio_codec.h"
#include <esp_log.h>
#include <esp_codec_dev.h>
#include <esp_codec_dev_defaults.h>
#include <adc_mic.h>
#include "settings.h"
#include "bsp/esp-bsp.h"

static const char *TAG = "AdcPdmC5";

AdcPdmAudioCodec::AdcPdmAudioCodec(int input_sample_rate, int output_sample_rate,
    uint32_t adc_mic_channel, gpio_num_t pdm_speak_p, gpio_num_t pdm_speak_n, gpio_num_t pa_ctl)
{
    input_reference_ = false;
    input_sample_rate_ = input_sample_rate;
    output_sample_rate_ = output_sample_rate;

    input_dev_ = bsp_audio_codec_microphone_init();
    output_dev_ = bsp_audio_codec_speaker_init();

    if (pa_ctl != GPIO_NUM_NC) {
        pa_ctrl_pin_ = pa_ctl;
        gpio_config_t io_conf = {};
        io_conf.intr_type = GPIO_INTR_DISABLE;
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pin_bit_mask = (1ULL << pa_ctrl_pin_);
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        gpio_config(&io_conf);
    }

    gpio_set_drive_capability(pdm_speak_p, GPIO_DRIVE_CAP_0);

    ESP_LOGI(TAG, "Initialized ADC+PDM for C5 (in=%dHz, out=%dHz)", input_sample_rate_, output_sample_rate_);
}

AdcPdmAudioCodec::~AdcPdmAudioCodec() {
    ESP_ERROR_CHECK(esp_codec_dev_close(output_dev_));
    esp_codec_dev_delete(output_dev_);
    ESP_ERROR_CHECK(esp_codec_dev_close(input_dev_));
    esp_codec_dev_delete(input_dev_);
}

void AdcPdmAudioCodec::SetOutputVolume(int volume) {
    ESP_ERROR_CHECK(esp_codec_dev_set_out_vol(output_dev_, volume));
    AudioCodec::SetOutputVolume(volume);
}

void AdcPdmAudioCodec::EnableInput(bool enable) {
    if (enable == input_enabled_) return;
    if (enable) {
        esp_codec_dev_sample_info_t fs = {
            .bits_per_sample = 16,
            .channel = 1,
            .channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0),
            .sample_rate = (uint32_t)input_sample_rate_,
            .mclk_multiple = 0,
        };
        ESP_ERROR_CHECK(esp_codec_dev_open(input_dev_, &fs));
    } else {
        ESP_ERROR_CHECK(esp_codec_dev_close(input_dev_));
    }
    AudioCodec::EnableInput(enable);
}

void AdcPdmAudioCodec::EnableOutput(bool enable) {
    if (enable == output_enabled_) return;
    if (enable) {
        esp_codec_dev_sample_info_t fs = {
            .bits_per_sample = 16,
            .channel = 1,
            .channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0),
            .sample_rate = (uint32_t)output_sample_rate_,
            .mclk_multiple = 0,
        };
        ESP_ERROR_CHECK(esp_codec_dev_open(output_dev_, &fs));
        ESP_ERROR_CHECK(esp_codec_dev_set_out_vol(output_dev_, output_volume_));
        if (pa_ctrl_pin_ != GPIO_NUM_NC) gpio_set_level(pa_ctrl_pin_, 1);
    } else {
        if (pa_ctrl_pin_ != GPIO_NUM_NC) gpio_set_level(pa_ctrl_pin_, 0);
        ESP_ERROR_CHECK(esp_codec_dev_close(output_dev_));
    }
    AudioCodec::EnableOutput(enable);
}

int AdcPdmAudioCodec::Read(int16_t* dest, int samples) {
    if (input_enabled_) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_codec_dev_read(input_dev_, (void*)dest, samples * sizeof(int16_t)));
    }
    return samples;
}

int AdcPdmAudioCodec::Write(const int16_t* data, int samples) {
    if (output_enabled_) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_codec_dev_write(output_dev_, (void*)data, samples * sizeof(int16_t)));
    }
    return samples;
}

void AdcPdmAudioCodec::Start() {
    if (input_enabled_ || output_enabled_) {
        ESP_LOGI(TAG, "Audio already started");
        return;
    }


    EnableInput(true);
    EnableOutput(true);
    ESP_LOGI(TAG, "Audio started");
} 