#pragma once
#include "sdkconfig.h"

#include <lvgl.h>
#include <memory>
#include <mutex>
#include <vector>

#include "camera.h"
#include "jpg/image_to_jpeg.h"
#include "esp_video_init.h"

class EspVideo : public Camera {
private:
    struct FrameBuffer {
        uint8_t *data = nullptr;
        size_t len = 0;
        uint16_t width = 0;
        uint16_t height = 0;
        v4l2_pix_fmt_t format = 0;
    } frame_;
    v4l2_pix_fmt_t sensor_format_ = 0;
#ifdef CONFIG_XIAOZHI_ENABLE_ROTATE_CAMERA_IMAGE
    uint16_t sensor_width_ = 0;
    uint16_t sensor_height_ = 0;
#endif  // CONFIG_XIAOZHI_ENABLE_ROTATE_CAMERA_IMAGE
    int video_fd_ = -1;
    bool is_spi_video_device_ = false;
    bool streaming_on_ = false;
    bool owns_video_stack_ = false;
    struct MmapBuffer { void *start = nullptr; size_t length = 0; };
    std::vector<MmapBuffer> mmap_buffers_;
    std::string explain_url_;
    std::string explain_token_;
    std::mutex mutex_;

    void InitializeFromDevice(const char* video_device_name);
    void FreeFrameBuffer(FrameBuffer& frame);
    bool CloneFrameBuffer(const FrameBuffer& src, FrameBuffer& dst);
    bool CaptureFrameLocked(FrameBuffer& frame);
    bool ShowPreviewFrame(const FrameBuffer& frame);
    bool EncodeFrameToJpeg(const FrameBuffer& frame, std::string& jpeg_data, uint8_t quality) const;

public:
    EspVideo(const esp_video_init_config_t& config);
    EspVideo(const char* video_device_name, bool owns_video_stack = false);
    ~EspVideo();

    virtual void SetExplainUrl(const std::string& url, const std::string& token);
    virtual bool Capture();
    bool CaptureToRgb565(std::vector<uint16_t>& rgb565_data, uint16_t& width, uint16_t& height);
    bool CaptureToJpeg(std::string& jpeg_data, uint8_t quality = 80);
    // 翻转控制函数
    virtual bool SetHMirror(bool enabled) override;
    virtual bool SetVFlip(bool enabled) override;
    virtual std::string Explain(const std::string& question);
};
