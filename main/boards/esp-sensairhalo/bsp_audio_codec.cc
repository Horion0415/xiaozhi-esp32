#include "bsp_audio_codec.h"

#include "bsp/esp_sensair_halo.h"

#include <esp_log.h>

namespace {

constexpr char TAG[] = "BspAudioCodec";
constexpr uint32_t kAudioSampleRate = 16000;

esp_codec_dev_sample_info_t MakeSampleInfo(uint32_t sample_rate) {
    return {
        .bits_per_sample = 16,
        .channel = 1,
        .channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0),
        .sample_rate = sample_rate,
        .mclk_multiple = 0,
    };
}

}  // namespace

BspAudioCodec::BspAudioCodec() {
    duplex_ = false;
    input_reference_ = false;
    input_channels_ = 1;
    output_channels_ = 1;
    input_sample_rate_ = kAudioSampleRate;
    output_sample_rate_ = kAudioSampleRate;

    ESP_ERROR_CHECK(bsp_speaker_init(&output_dev_));
    ESP_ERROR_CHECK(bsp_microphone_init(&input_dev_));
    ESP_LOGI(TAG, "BSP audio codec initialized");
}

BspAudioCodec::~BspAudioCodec() {
    if (output_enabled_) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_codec_dev_close(output_dev_));
    }
    if (input_enabled_) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_codec_dev_close(input_dev_));
    }
    bsp_microphone_deinit();
    bsp_speaker_deinit();
}

void BspAudioCodec::SetOutputVolume(int volume) {
    if (output_dev_ != nullptr) {
        ESP_ERROR_CHECK(esp_codec_dev_set_out_vol(output_dev_, volume));
    }
    AudioCodec::SetOutputVolume(volume);
}

void BspAudioCodec::SetInputGain(float gain) {
    if (input_dev_ != nullptr && input_enabled_) {
        ESP_ERROR_CHECK(esp_codec_dev_set_in_gain(input_dev_, gain));
    }
    AudioCodec::SetInputGain(gain);
}

void BspAudioCodec::EnableInput(bool enable) {
    if (enable == input_enabled_ || input_dev_ == nullptr) {
        return;
    }

    ESP_LOGI(TAG, "Switch input %s, sample_rate=%lu", enable ? "on" : "off",
             static_cast<unsigned long>(input_sample_rate_));
    if (enable) {
        auto sample_info = MakeSampleInfo(input_sample_rate_);
        ESP_ERROR_CHECK(esp_codec_dev_open(input_dev_, &sample_info));
        ESP_ERROR_CHECK(esp_codec_dev_set_in_gain(input_dev_, input_gain_));
    } else {
        ESP_ERROR_CHECK(esp_codec_dev_close(input_dev_));
    }

    AudioCodec::EnableInput(enable);
}

void BspAudioCodec::EnableOutput(bool enable) {
    if (enable == output_enabled_ || output_dev_ == nullptr) {
        return;
    }

    ESP_LOGI(TAG, "Switch output %s, sample_rate=%lu", enable ? "on" : "off",
             static_cast<unsigned long>(output_sample_rate_));
    if (enable) {
        auto sample_info = MakeSampleInfo(output_sample_rate_);
        ESP_ERROR_CHECK(esp_codec_dev_open(output_dev_, &sample_info));
        ESP_ERROR_CHECK(esp_codec_dev_set_out_vol(output_dev_, output_volume_));
        ESP_ERROR_CHECK(bsp_pa_enable(true));
    } else {
        ESP_ERROR_CHECK(bsp_pa_enable(false));
        ESP_ERROR_CHECK(esp_codec_dev_close(output_dev_));
    }

    AudioCodec::EnableOutput(enable);
}

void BspAudioCodec::Start() {
    AudioCodec::Start();
    EnableInput(true);
    ESP_LOGI(TAG, "BSP audio codec started, output remains deferred until playback");
}

int BspAudioCodec::Read(int16_t* dest, int samples) {
    if (input_enabled_ && input_dev_ != nullptr) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_codec_dev_read(input_dev_, dest, samples * sizeof(int16_t)));
    }
    return samples;
}

int BspAudioCodec::Write(const int16_t* data, int samples) {
    if (output_enabled_ && output_dev_ != nullptr) {
        // Internal software volume updates the playback buffer in place.
        write_buf_.assign(data, data + samples);
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_codec_dev_write(output_dev_, write_buf_.data(), samples * sizeof(int16_t)));
    }
    return samples;
}
