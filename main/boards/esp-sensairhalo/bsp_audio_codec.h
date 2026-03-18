#ifndef BSP_AUDIO_CODEC_H
#define BSP_AUDIO_CODEC_H

#include "audio_codec.h"

#include <esp_codec_dev.h>
#include <vector>

class BspAudioCodec : public AudioCodec {
private:
    esp_codec_dev_handle_t output_dev_ = nullptr;
    esp_codec_dev_handle_t input_dev_ = nullptr;
    std::vector<int16_t> write_buf_;

    virtual int Read(int16_t* dest, int samples) override;
    virtual int Write(const int16_t* data, int samples) override;

public:
    BspAudioCodec();
    ~BspAudioCodec() override;

    void SetOutputVolume(int volume) override;
    void SetInputGain(float gain) override;
    void EnableInput(bool enable) override;
    void EnableOutput(bool enable) override;
    void Start() override;
};

#endif // BSP_AUDIO_CODEC_H
