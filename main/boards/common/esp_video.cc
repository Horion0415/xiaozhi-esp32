#include "sdkconfig.h"

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <unistd.h>
#include <errno.h>
#include <esp_heap_caps.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>

#include "esp_imgfx_color_convert.h"
#include "esp_video_device.h"
#include "esp_video_init.h"
#include "linux/videodev2.h"

#include "board.h"
#include "display.h"
#include "esp_video.h"
#include "esp_jpeg_common.h"
#include "jpg/image_to_jpeg.h"
#include "jpg/jpeg_to_image.h"
#include "lvgl_display.h"
#include "mcp_server.h"
#include "system_info.h"

#ifdef CONFIG_XIAOZHI_ENABLE_CAMERA_DEBUG_MODE
#undef LOG_LOCAL_LEVEL
#define LOG_LOCAL_LEVEL MAX(CONFIG_LOG_DEFAULT_LEVEL, ESP_LOG_DEBUG)
#endif  // CONFIG_XIAOZHI_ENABLE_CAMERA_DEBUG_MODE
#include <esp_log.h> // should be after LOCAL_LOG_LEVEL definition

#ifdef CONFIG_XIAOZHI_ENABLE_ROTATE_CAMERA_IMAGE
#ifdef CONFIG_IDF_TARGET_ESP32P4
#include "driver/ppa.h"
#if defined(CONFIG_XIAOZHI_CAMERA_IMAGE_ROTATION_ANGLE_90)
#define IMAGE_ROTATION_ANGLE (PPA_SRM_ROTATION_ANGLE_270)
#elif defined(CONFIG_XIAOZHI_CAMERA_IMAGE_ROTATION_ANGLE_270)
#define IMAGE_ROTATION_ANGLE (PPA_SRM_ROTATION_ANGLE_90)
#else
#error "CONFIG_XIAOZHI_CAMERA_IMAGE_ROTATION_ANGLE is not set"
#endif  // angle
#else   // target
#include "esp_imgfx_rotate.h"
#if defined(CONFIG_XIAOZHI_CAMERA_IMAGE_ROTATION_ANGLE_90)
#define IMAGE_ROTATION_ANGLE (90)
#elif defined(CONFIG_XIAOZHI_CAMERA_IMAGE_ROTATION_ANGLE_270)
#define IMAGE_ROTATION_ANGLE (270)
#else
#error "CONFIG_XIAOZHI_CAMERA_IMAGE_ROTATION_ANGLE is not set"
#endif  // angle
#endif  // target
#endif  // CONFIG_XIAOZHI_ENABLE_ROTATE_CAMERA_IMAGE


#define TAG "EspVideo"

#if defined(CONFIG_CAMERA_SENSOR_SWAP_PIXEL_BYTE_ORDER) || defined(CONFIG_XIAOZHI_ENABLE_CAMERA_ENDIANNESS_SWAP)
#warning \
    "CAMERA_SENSOR_SWAP_PIXEL_BYTE_ORDER or CONFIG_XIAOZHI_ENABLE_CAMERA_ENDIANNESS_SWAP is enabled, which may cause image corruption in YUV422 format!"
#endif

#if CONFIG_XIAOZHI_ENABLE_CAMERA_DEBUG_MODE
#define CAM_PRINT_FOURCC(pixelformat)       \
    char fourcc[5];                         \
    fourcc[0] = pixelformat & 0xFF;         \
    fourcc[1] = (pixelformat >> 8) & 0xFF;  \
    fourcc[2] = (pixelformat >> 16) & 0xFF; \
    fourcc[3] = (pixelformat >> 24) & 0xFF; \
    fourcc[4] = '\0';                       \
    ESP_LOGD(TAG, "FOURCC: '%c%c%c%c'", fourcc[0], fourcc[1], fourcc[2], fourcc[3]);

// for compatibility with old esp_video version
#ifndef MAP_FAILED
#define MAP_FAILED nullptr
#endif

__attribute__((weak)) esp_err_t esp_video_deinit(void) {
    return ESP_ERR_NOT_SUPPORTED;
}
// end of for compatibility with old esp_video version

static void log_available_video_devices() {
    for (int i = 0; i < 50; i++) {
        char path[16];
        snprintf(path, sizeof(path), "/dev/video%d", i);
        int fd = open(path, O_RDONLY);
        if (fd >= 0) {
            ESP_LOGD(TAG, "found video device: %s", path);
            close(fd);
        }
    }
}
#else
#define CAM_PRINT_FOURCC(pixelformat) (void)0;
#endif  // CONFIG_XIAOZHI_ENABLE_CAMERA_DEBUG_MODE

static bool is_mipi_csi_video_device(const char* video_device_name) {
    return strcmp(video_device_name, ESP_VIDEO_MIPI_CSI_DEVICE_NAME) == 0;
}

static bool is_spi_video_device(const char* video_device_name) {
    return strcmp(video_device_name, ESP_VIDEO_SPI_DEVICE_NAME) == 0;
}

extern "C" __attribute__((weak)) esp_err_t bsp_camera_apply_default_controls(int fd);

#if CONFIG_CAMERA_BF3901
static bool decode_bf3901_rgb565_frame(const uint8_t* raw_frame, size_t raw_frame_len, uint16_t width, uint16_t height,
                                       uint8_t* decoded_frame, size_t decoded_frame_len) {
    static const uint8_t kFrameHeader[] = {0xFF, 0xFF, 0xFF, 0x00};
    static const uint8_t kLineHeaderPrefix[] = {0xFF, 0xFF, 0xFF, 0x40};
    constexpr size_t kFrameHeaderSize = 4;
    constexpr size_t kLineHeaderSize = 6;

    if (raw_frame == nullptr || decoded_frame == nullptr || width == 0 || height == 0) {
        return false;
    }

    const size_t decoded_bytes = static_cast<size_t>(width) * height * sizeof(uint16_t);
    if (decoded_frame_len < decoded_bytes) {
        ESP_LOGE(TAG, "BF3901 decoded frame buffer too small: got=%u need=%u", static_cast<unsigned>(decoded_frame_len),
                 static_cast<unsigned>(decoded_bytes));
        return false;
    }

    if (raw_frame_len == decoded_bytes) {
        memcpy(decoded_frame, raw_frame, decoded_bytes);
        return true;
    }

    const size_t raw_line_bytes = (static_cast<size_t>(width) * sizeof(uint16_t)) + kLineHeaderSize;
    const size_t min_raw_bytes = kFrameHeaderSize + (raw_line_bytes * height);
    if (raw_frame_len < min_raw_bytes) {
        ESP_LOGE(TAG, "BF3901 raw frame too small: got=%u need=%u", static_cast<unsigned>(raw_frame_len),
                 static_cast<unsigned>(min_raw_bytes));
        return false;
    }

    if (memcmp(raw_frame, kFrameHeader, sizeof(kFrameHeader)) != 0) {
        ESP_LOGE(TAG, "Unexpected BF3901 frame header");
        return false;
    }

    const uint8_t* src = raw_frame + kFrameHeaderSize;
    uint8_t* dst = decoded_frame;
    for (uint32_t row = 0; row < height; row++) {
        if (memcmp(src, kLineHeaderPrefix, sizeof(kLineHeaderPrefix)) != 0) {
            ESP_LOGE(TAG, "Unexpected BF3901 line header at row=%u", static_cast<unsigned>(row));
            return false;
        }

        memcpy(dst, src + kLineHeaderSize, static_cast<size_t>(width) * sizeof(uint16_t));
        src += raw_line_bytes;
        dst += static_cast<size_t>(width) * sizeof(uint16_t);
    }

    return true;
}
#endif  // CONFIG_CAMERA_BF3901

EspVideo::EspVideo(const esp_video_init_config_t& config) {
    if (esp_video_init(&config) != ESP_OK) {
        ESP_LOGE(TAG, "esp_video_init failed");
        return;
    }
    owns_video_stack_ = true;

#ifdef CONFIG_XIAOZHI_ENABLE_CAMERA_DEBUG_MODE
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
#endif  // CONFIG_XIAOZHI_ENABLE_CAMERA_DEBUG_MODE

    const char* video_device_name = nullptr;

    if (false) { /* 用于构建 else if */
    }
#if CONFIG_ESP_VIDEO_ENABLE_MIPI_CSI_VIDEO_DEVICE
    else if (config.csi != nullptr) {
        video_device_name = ESP_VIDEO_MIPI_CSI_DEVICE_NAME;
    }
#endif
#if CONFIG_ESP_VIDEO_ENABLE_DVP_VIDEO_DEVICE
    else if (config.dvp != nullptr) {
        video_device_name = ESP_VIDEO_DVP_DEVICE_NAME;
    }
#endif
#if CONFIG_ESP_VIDEO_ENABLE_HW_JPEG_VIDEO_DEVICE
    else if (config.jpeg != nullptr) {
        video_device_name = ESP_VIDEO_JPEG_DEVICE_NAME;
    }
#endif
#if CONFIG_ESP_VIDEO_ENABLE_SPI_VIDEO_DEVICE
    else if (config.spi != nullptr) {
        video_device_name = ESP_VIDEO_SPI_DEVICE_NAME;
    }
#endif
#if CONFIG_ESP_VIDEO_ENABLE_USB_UVC_VIDEO_DEVICE
    else if (config.usb_uvc != nullptr) {
        video_device_name = ESP_VIDEO_USB_UVC_DEVICE_NAME(0);
    }
#endif

    if (video_device_name == nullptr) {
        ESP_LOGE(TAG, "no video device is enabled");
        return;
    }

    InitializeFromDevice(video_device_name);
}

EspVideo::EspVideo(const char* video_device_name, bool owns_video_stack) {
    owns_video_stack_ = owns_video_stack;

#ifdef CONFIG_XIAOZHI_ENABLE_CAMERA_DEBUG_MODE
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
#endif  // CONFIG_XIAOZHI_ENABLE_CAMERA_DEBUG_MODE

    if (video_device_name == nullptr || video_device_name[0] == '\0') {
        ESP_LOGE(TAG, "invalid video device name");
        return;
    }

    InitializeFromDevice(video_device_name);
}

void EspVideo::InitializeFromDevice(const char* video_device_name) {
    is_spi_video_device_ = is_spi_video_device(video_device_name);
    video_fd_ = open(video_device_name, O_RDWR);

    if (video_fd_ < 0) {
        ESP_LOGE(TAG, "open %s failed, errno=%d(%s)", video_device_name, errno, strerror(errno));
#if CONFIG_XIAOZHI_ENABLE_CAMERA_DEBUG_MODE
        log_available_video_devices();
#endif  // CONFIG_XIAOZHI_ENABLE_CAMERA_DEBUG_MODE
        return;
    }

    if (is_spi_video_device_ && bsp_camera_apply_default_controls != nullptr) {
        if (bsp_camera_apply_default_controls(video_fd_) != ESP_OK) {
            ESP_LOGW(TAG, "Failed to apply BSP default camera controls");
        }
    }

    struct v4l2_capability cap = {};
    if (ioctl(video_fd_, VIDIOC_QUERYCAP, &cap) != 0) {
        ESP_LOGE(TAG, "VIDIOC_QUERYCAP failed, errno=%d(%s)", errno, strerror(errno));
        close(video_fd_);
        video_fd_ = -1;
        return;
    }

    ESP_LOGD(
        TAG,
        "VIDIOC_QUERYCAP: driver=%s, card=%s, bus_info=%s, version=0x%08lx, capabilities=0x%08lx, device_caps=0x%08lx",
        cap.driver, cap.card, cap.bus_info, cap.version, cap.capabilities, cap.device_caps);

    struct v4l2_format format = {};
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(video_fd_, VIDIOC_G_FMT, &format) != 0) {
        ESP_LOGE(TAG, "VIDIOC_G_FMT failed, errno=%d(%s)", errno, strerror(errno));
        close(video_fd_);
        video_fd_ = -1;
        return;
    }
    ESP_LOGD(TAG, "VIDIOC_G_FMT: pixelformat=0x%08lx, width=%ld, height=%ld", format.fmt.pix.pixelformat,
             format.fmt.pix.width, format.fmt.pix.height);
    CAM_PRINT_FOURCC(format.fmt.pix.pixelformat);

    struct v4l2_format setformat = {};
    setformat.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
#ifdef CONFIG_XIAOZHI_ENABLE_ROTATE_CAMERA_IMAGE
    sensor_width_ = format.fmt.pix.width;
    sensor_height_ = format.fmt.pix.height;
#endif  // CONFIG_XIAOZHI_ENABLE_ROTATE_CAMERA_IMAGE
    setformat.fmt.pix.width = format.fmt.pix.width;
    setformat.fmt.pix.height = format.fmt.pix.height;

    struct v4l2_fmtdesc fmtdesc = {};
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmtdesc.index = 0;
    uint32_t best_fmt = 0;
    int best_rank = 1 << 30;  // large number

    // 注: 当前版本 esp_video 中 YUV422P 实际输出为 YUYV。
#if defined(CONFIG_XIAOZHI_ENABLE_ROTATE_CAMERA_IMAGE) && defined(CONFIG_SOC_PPA_SUPPORTED)
    auto get_rank = [](uint32_t fmt) -> int {
        switch (fmt) {
            case V4L2_PIX_FMT_RGB24:
                return 0;
            case V4L2_PIX_FMT_RGB565:
                return 1;
#ifdef CONFIG_XIAOZHI_ENABLE_HARDWARE_JPEG_ENCODER
            case V4L2_PIX_FMT_YUV420:  // 软件 JPEG 编码器不支持 YUV420 格式
                return 2;
#endif  // CONFIG_XIAOZHI_ENABLE_HARDWARE_JPEG_ENCODER
            case V4L2_PIX_FMT_GREY:
            case V4L2_PIX_FMT_YUV422P:
            default:
                return 1 << 29;  // unsupported
        }
    };
#else
    auto get_rank = [](uint32_t fmt) -> int {
        switch (fmt) {
            case V4L2_PIX_FMT_YUV422P:
                return 10;
            case V4L2_PIX_FMT_RGB565:
                return 11;
            case V4L2_PIX_FMT_RGB24:
                return 12;
#ifdef CONFIG_XIAOZHI_ENABLE_HARDWARE_JPEG_ENCODER
            case V4L2_PIX_FMT_YUV420:
                return 13;
#endif  // CONFIG_XIAOZHI_ENABLE_HARDWARE_JPEG_ENCODER
#ifdef CONFIG_XIAOZHI_CAMERA_ALLOW_JPEG_INPUT
            case V4L2_PIX_FMT_JPEG:
                return 5;
#endif  // CONFIG_XIAOZHI_CAMERA_ALLOW_JPEG_INPUT
            case V4L2_PIX_FMT_GREY:
                return 20;
            default:
                return 1 << 29;  // unsupported
        }
    };
#endif
    while (ioctl(video_fd_, VIDIOC_ENUM_FMT, &fmtdesc) == 0) {
        ESP_LOGD(TAG, "VIDIOC_ENUM_FMT: pixelformat=0x%08lx, description=%s", fmtdesc.pixelformat, fmtdesc.description);
        CAM_PRINT_FOURCC(fmtdesc.pixelformat);
        int rank = get_rank(fmtdesc.pixelformat);
        if (rank < best_rank) {
            best_rank = rank;
            best_fmt = fmtdesc.pixelformat;
        }
        fmtdesc.index++;
    }
    if (best_rank < (1 << 29)) {
        setformat.fmt.pix.pixelformat = best_fmt;
        sensor_format_ = best_fmt;
    }

    if (!setformat.fmt.pix.pixelformat) {
        ESP_LOGE(TAG, "no supported pixel format found");
        close(video_fd_);
        video_fd_ = -1;
        sensor_format_ = 0;
        return;
    }

    ESP_LOGD(TAG, "selected pixel format: 0x%08lx", setformat.fmt.pix.pixelformat);

    if (ioctl(video_fd_, VIDIOC_S_FMT, &setformat) != 0) {
        ESP_LOGE(TAG, "VIDIOC_S_FMT failed, errno=%d(%s)", errno, strerror(errno));
        close(video_fd_);
        video_fd_ = -1;
        sensor_format_ = 0;
        return;
    }

#ifdef CONFIG_XIAOZHI_ENABLE_ROTATE_CAMERA_IMAGE
    frame_.width = setformat.fmt.pix.height;
    frame_.height = setformat.fmt.pix.width;
#else
    frame_.width = setformat.fmt.pix.width;
    frame_.height = setformat.fmt.pix.height;
#endif

    // 申请缓冲并mmap
    struct v4l2_requestbuffers req = {};
    uint32_t min_buffer_count = 1;
    if (is_spi_video_device_) {
        req.count = 3;
        min_buffer_count = 2;
    } else if (is_mipi_csi_video_device(video_device_name)) {
        req.count = 2;
    } else {
        req.count = 1;
    }
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(video_fd_, VIDIOC_REQBUFS, &req) != 0) {
        ESP_LOGE(TAG, "VIDIOC_REQBUFS failed");
        close(video_fd_);
        video_fd_ = -1;
        sensor_format_ = 0;
        return;
    }
    if (req.count < min_buffer_count) {
        ESP_LOGE(TAG, "insufficient camera buffers: requested=%u got=%u need_at_least=%u",
                 is_spi_video_device_ ? 3U : min_buffer_count, req.count, min_buffer_count);
        close(video_fd_);
        video_fd_ = -1;
        sensor_format_ = 0;
        return;
    }
    mmap_buffers_.resize(req.count);
    for (uint32_t i = 0; i < req.count; i++) {
        struct v4l2_buffer buf = {};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (ioctl(video_fd_, VIDIOC_QUERYBUF, &buf) != 0) {
            ESP_LOGE(TAG, "VIDIOC_QUERYBUF failed");
            close(video_fd_);
            video_fd_ = -1;
            sensor_format_ = 0;
            return;
        }
        void* start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, video_fd_, buf.m.offset);
        if (start == MAP_FAILED) {
            ESP_LOGE(TAG, "mmap failed");
            close(video_fd_);
            video_fd_ = -1;
            sensor_format_ = 0;
            return;
        }
        mmap_buffers_[i].start = start;
        mmap_buffers_[i].length = buf.length;

        if (ioctl(video_fd_, VIDIOC_QBUF, &buf) != 0) {
            ESP_LOGE(TAG, "VIDIOC_QBUF failed");
            close(video_fd_);
            video_fd_ = -1;
            sensor_format_ = 0;
            return;
        }
    }

    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(video_fd_, VIDIOC_STREAMON, &type) != 0) {
        ESP_LOGE(TAG, "VIDIOC_STREAMON failed");
        close(video_fd_);
        video_fd_ = -1;
        sensor_format_ = 0;
        return;
    }

#ifdef CONFIG_ESP_VIDEO_ENABLE_ISP_VIDEO_DEVICE
    // 当启用 ISP 时，ISP 需要一些照片来初始化参数，因此开启后后台拍摄5s照片并丢弃
    xTaskCreate(
        [](void* arg) {
            EspVideo* self = static_cast<EspVideo*>(arg);
            uint16_t capture_count = 0;
            TickType_t start = xTaskGetTickCount();
            TickType_t duration = 5000 / portTICK_PERIOD_MS;  // 5s
            while ((xTaskGetTickCount() - start) < duration) {
                struct v4l2_buffer buf = {};
                buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                buf.memory = V4L2_MEMORY_MMAP;
                if (ioctl(self->video_fd_, VIDIOC_DQBUF, &buf) != 0) {
                    ESP_LOGE(TAG, "VIDIOC_DQBUF failed during init");
                    vTaskDelay(10 / portTICK_PERIOD_MS);
                    continue;
                }
                if (ioctl(self->video_fd_, VIDIOC_QBUF, &buf) != 0) {
                    ESP_LOGE(TAG, "VIDIOC_QBUF failed during init");
                }
                capture_count++;
            }
            ESP_LOGI(TAG, "Camera init success, captured %d frames in %lums", capture_count,
                     (unsigned long)((xTaskGetTickCount() - start) * portTICK_PERIOD_MS));
            self->streaming_on_ = true;
            vTaskDelete(NULL);
        },
        "CameraInitTask", 4096, this, 5, nullptr);
#else
    ESP_LOGI(TAG, "Camera init success");
    streaming_on_ = true;
#endif  // CONFIG_ESP_VIDEO_ENABLE_ISP_VIDEO_DEVICE
}

EspVideo::~EspVideo() {
    FreeFrameBuffer(frame_);
    if (streaming_on_ && video_fd_ >= 0) {
        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(video_fd_, VIDIOC_STREAMOFF, &type);
    }
    for (auto& b : mmap_buffers_) {
        if (b.start && b.length) {
            munmap(b.start, b.length);
        }
    }
    if (video_fd_ >= 0) {
        close(video_fd_);
        video_fd_ = -1;
    }
    sensor_format_ = 0;
    if (owns_video_stack_) {
        esp_video_deinit();
    }
}

void EspVideo::FreeFrameBuffer(FrameBuffer& frame) {
    if (frame.data != nullptr) {
        heap_caps_free(frame.data);
        frame.data = nullptr;
    }
    frame.len = 0;
    frame.format = 0;
}

bool EspVideo::CloneFrameBuffer(const FrameBuffer& src, FrameBuffer& dst) {
    if (src.data == nullptr || src.len == 0) {
        return false;
    }

    FreeFrameBuffer(dst);
    dst.width = src.width;
    dst.height = src.height;
    dst.len = src.len;
    dst.format = src.format;
    dst.data = static_cast<uint8_t*>(heap_caps_malloc(src.len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (dst.data == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate %u bytes for frame clone", static_cast<unsigned>(src.len));
        dst.len = 0;
        dst.format = 0;
        return false;
    }

    memcpy(dst.data, src.data, src.len);
    return true;
}

void EspVideo::SetExplainUrl(const std::string& url, const std::string& token) {
    explain_url_ = url;
    explain_token_ = token;
}

bool EspVideo::CaptureFrameLocked(FrameBuffer& frame) {
    struct FrameCleanupGuard {
        FrameBuffer& frame;
        bool keep = false;

        ~FrameCleanupGuard() {
            if (keep || frame.data == nullptr) {
                return;
            }
            heap_caps_free(frame.data);
            frame.data = nullptr;
            frame.len = 0;
            frame.format = 0;
        }
    } cleanup_guard{frame};

    FreeFrameBuffer(frame);
    frame.width = frame_.width;
    frame.height = frame_.height;

    if (!streaming_on_ || video_fd_ < 0) {
        return false;
    }

    if (frame.width == 0 || frame.height == 0) {
        ESP_LOGE(TAG, "frame size is invalid");
        return false;
    }

    int valid_frames = 0;
    for (int i = 0; i < 10; i++) {
        struct v4l2_buffer buf = {};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        if (ioctl(video_fd_, VIDIOC_DQBUF, &buf) != 0) {
            ESP_LOGE(TAG, "VIDIOC_DQBUF failed");
            return false;
        }
        if (buf.bytesused == 0) {
            ESP_LOGD(TAG, "skip empty frame %d", i);
            if (ioctl(video_fd_, VIDIOC_QBUF, &buf) != 0) {
                ESP_LOGE(TAG, "VIDIOC_QBUF failed on empty frame");
            }
            continue;
        }
        valid_frames++;
        if (valid_frames == 3) {
            // 保存帧副本到PSRAM
            size_t frame_copy_len = buf.bytesused;
#if CONFIG_CAMERA_BF3901
#ifdef CONFIG_XIAOZHI_ENABLE_ROTATE_CAMERA_IMAGE
            const uint16_t sensor_frame_width = sensor_width_;
            const uint16_t sensor_frame_height = sensor_height_;
#else
            const uint16_t sensor_frame_width = frame.width;
            const uint16_t sensor_frame_height = frame.height;
#endif  // CONFIG_XIAOZHI_ENABLE_ROTATE_CAMERA_IMAGE
            const bool needs_bf3901_decode =
                is_spi_video_device_ && sensor_format_ == V4L2_PIX_FMT_RGB565 &&
                sensor_frame_width > 0 && sensor_frame_height > 0 &&
                buf.bytesused != static_cast<size_t>(sensor_frame_width) * sensor_frame_height * sizeof(uint16_t);
            if (needs_bf3901_decode) {
                frame_copy_len = static_cast<size_t>(sensor_frame_width) * sensor_frame_height * sizeof(uint16_t);
            }
#else
            const bool needs_bf3901_decode = false;
#endif  // CONFIG_CAMERA_BF3901
            frame.len = frame_copy_len;
            frame.data = static_cast<uint8_t*>(heap_caps_malloc(frame.len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
            if (!frame.data) {
                ESP_LOGE(TAG, "alloc frame copy failed: need allocate %u bytes", static_cast<unsigned>(frame.len));
                if (ioctl(video_fd_, VIDIOC_QBUF, &buf) != 0) {
                    ESP_LOGE(TAG, "Cleanup: VIDIOC_QBUF failed");
                }
                return false;
            }

#if CONFIG_XIAOZHI_ENABLE_CAMERA_DEBUG_MODE
#ifdef CONFIG_XIAOZHI_ENABLE_ROTATE_CAMERA_IMAGE
            ESP_LOGD(TAG, "mmap_buffers_[buf.index].length = %d, sensor_width = %d, sensor_height = %d",
                     mmap_buffers_[buf.index].length, sensor_width_, sensor_height_);
#else
            ESP_LOGD(TAG, "mmap_buffers_[buf.index].length = %d, frame.width = %d, frame.height = %d",
                     mmap_buffers_[buf.index].length, frame.width, frame.height);
#endif  // CONFIG_XIAOZHI_ENABLE_ROTATE_CAMERA_IMAGE
            ESP_LOG_BUFFER_HEXDUMP(TAG, mmap_buffers_[buf.index].start, MIN(mmap_buffers_[buf.index].length, 256),
                                   ESP_LOG_DEBUG);
#endif  // CONFIG_XIAOZHI_ENABLE_CAMERA_DEBUG_MODE

            switch (sensor_format_) {
                case V4L2_PIX_FMT_RGB565: {
#if CONFIG_CAMERA_BF3901
                    if (needs_bf3901_decode) {
                        if (!decode_bf3901_rgb565_frame(static_cast<const uint8_t*>(mmap_buffers_[buf.index].start),
                                                        buf.bytesused, sensor_frame_width, sensor_frame_height,
                                                        frame.data, frame.len)) {
                            if (ioctl(video_fd_, VIDIOC_QBUF, &buf) != 0) {
                                ESP_LOGE(TAG, "Cleanup: VIDIOC_QBUF failed");
                            }
                            return false;
                        }
#ifdef CONFIG_XIAOZHI_ENABLE_CAMERA_ENDIANNESS_SWAP
                        auto pixels = reinterpret_cast<uint16_t*>(frame.data);
                        size_t pixel_count = frame.len / sizeof(uint16_t);
                        for (size_t i = 0; i < pixel_count; i++) {
                            pixels[i] = __builtin_bswap16(pixels[i]);
                        }
#endif  // CONFIG_XIAOZHI_ENABLE_CAMERA_ENDIANNESS_SWAP
                    } else
#endif  // CONFIG_CAMERA_BF3901
                    {
#ifdef CONFIG_XIAOZHI_ENABLE_CAMERA_ENDIANNESS_SWAP
                        auto src16 = (uint16_t*)mmap_buffers_[buf.index].start;
                        auto dst16 = (uint16_t*)frame.data;
                        size_t count = frame.len / 2;
                        for (size_t i = 0; i < count; i++) {
                            dst16[i] = __builtin_bswap16(src16[i]);
                        }
#else
                        memcpy(frame.data, mmap_buffers_[buf.index].start,
                               MIN(mmap_buffers_[buf.index].length, frame.len));
#endif  // CONFIG_XIAOZHI_ENABLE_CAMERA_ENDIANNESS_SWAP
                    }
                    frame.format = sensor_format_;
                    break;
                }
                case V4L2_PIX_FMT_RGB24:
                case V4L2_PIX_FMT_YUYV:
                case V4L2_PIX_FMT_YUV420:
                case V4L2_PIX_FMT_GREY:
#ifdef CONFIG_XIAOZHI_CAMERA_ALLOW_JPEG_INPUT
                case V4L2_PIX_FMT_JPEG:
#endif  // CONFIG_XIAOZHI_CAMERA_ALLOW_JPEG_INPUT
#ifdef CONFIG_XIAOZHI_ENABLE_CAMERA_ENDIANNESS_SWAP
                    {
                        auto src16 = (uint16_t*)mmap_buffers_[buf.index].start;
                        auto dst16 = (uint16_t*)frame.data;
                        size_t count = frame.len / 2;
                        for (size_t i = 0; i < count; i++) {
                            dst16[i] = __builtin_bswap16(src16[i]);
                        }
                    }
#else
                    memcpy(frame.data, mmap_buffers_[buf.index].start, MIN(mmap_buffers_[buf.index].length, frame.len));
#endif  // CONFIG_XIAOZHI_ENABLE_CAMERA_ENDIANNESS_SWAP
                    frame.format = sensor_format_;
                    break;
                case V4L2_PIX_FMT_YUV422P: {
                    // 这个格式是 422 YUYV，不是 planer
                    frame.format = V4L2_PIX_FMT_YUYV;
#ifdef CONFIG_XIAOZHI_ENABLE_CAMERA_ENDIANNESS_SWAP
                    {
                        auto src16 = (uint16_t*)mmap_buffers_[buf.index].start;
                        auto dst16 = (uint16_t*)frame.data;
                        size_t count = (size_t)mmap_buffers_[buf.index].length / 2;
                        for (size_t i = 0; i < count; i++) {
                            dst16[i] = __builtin_bswap16(src16[i]);
                        }
                    }
#else
                    memcpy(frame.data, mmap_buffers_[buf.index].start,
                           MIN(mmap_buffers_[buf.index].length, frame.len));
#endif  // CONFIG_XIAOZHI_ENABLE_CAMERA_ENDIANNESS_SWAP
                    break;
                }
                case V4L2_PIX_FMT_RGB565X: {
                    // 大端序的 RGB565 需要转换为小端序
                    // 目前 esp_video 的大小端都会返回格式为 RGB565，不会返回格式为 RGB565X，此 case 用于未来版本兼容
                    auto src16 = (uint16_t*)mmap_buffers_[buf.index].start;
                    auto dst16 = (uint16_t*)frame.data;
                    size_t pixel_count = static_cast<size_t>(frame.width) * static_cast<size_t>(frame.height);
                    for (size_t i = 0; i < pixel_count; i++) {
                        dst16[i] = __builtin_bswap16(src16[i]);
                    }
                    frame.format = V4L2_PIX_FMT_RGB565;
                    break;
                }
                default:
                    ESP_LOGE(TAG, "unsupported sensor format: 0x%08lx", sensor_format_);
                    if (ioctl(video_fd_, VIDIOC_QBUF, &buf) != 0) {
                        ESP_LOGE(TAG, "Cleanup: VIDIOC_QBUF failed");
                    }
                    return false;
            }

#ifdef CONFIG_XIAOZHI_ENABLE_ROTATE_CAMERA_IMAGE
#ifndef CONFIG_SOC_PPA_SUPPORTED
            uint8_t* rotate_dst =
                (uint8_t*)heap_caps_aligned_alloc(64, frame.len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (rotate_dst == nullptr) {
                ESP_LOGE(TAG, "Failed to allocate memory for rotate image");
                if (ioctl(video_fd_, VIDIOC_QBUF, &buf) != 0) {
                    ESP_LOGE(TAG, "Cleanup: VIDIOC_QBUF failed");
                }
                return false;
            }
            uint8_t* rotate_src = frame.data;

            esp_imgfx_rotate_cfg_t rotate_cfg = {
                .in_res =
                    {
                        .width = static_cast<int16_t>(sensor_width_),
                        .height = static_cast<int16_t>(sensor_height_),
                    },
                .degree = IMAGE_ROTATION_ANGLE,
            };
            switch (frame.format) {
                case V4L2_PIX_FMT_RGB565:
                    rotate_cfg.in_pixel_fmt = ESP_IMGFX_PIXEL_FMT_RGB565_LE;
                    break;
                case V4L2_PIX_FMT_YUYV:
                    rotate_cfg.in_pixel_fmt = ESP_IMGFX_PIXEL_FMT_RGB565_LE;
                    break;
                case V4L2_PIX_FMT_GREY:
                    rotate_cfg.in_pixel_fmt = ESP_IMGFX_PIXEL_FMT_Y;
                    break;
                case V4L2_PIX_FMT_RGB24:
                    rotate_cfg.in_pixel_fmt = ESP_IMGFX_PIXEL_FMT_RGB888;
                    break;
                default:
                    ESP_LOGE(TAG, "unsupported sensor format: 0x%08lx", sensor_format_);
                    if (ioctl(video_fd_, VIDIOC_QBUF, &buf) != 0) {
                        ESP_LOGE(TAG, "Cleanup: VIDIOC_QBUF failed");
                    }
                    return false;
            }
            esp_imgfx_rotate_handle_t rotate_handle = nullptr;
            esp_imgfx_err_t imgfx_err = esp_imgfx_rotate_open(&rotate_cfg, &rotate_handle);
            if (imgfx_err != ESP_IMGFX_ERR_OK || rotate_handle == nullptr) {
                ESP_LOGE(TAG, "esp_imgfx_rotate_create failed");
                if (ioctl(video_fd_, VIDIOC_QBUF, &buf) != 0) {
                    ESP_LOGE(TAG, "Cleanup: VIDIOC_QBUF failed");
                }
                return false;
            }

            esp_imgfx_data_t rotate_input_data = {
                .data = rotate_src,
                .data_len = frame.len,
            };
            esp_imgfx_data_t rotate_output_data = {
                .data = rotate_dst,
                .data_len = frame.len,
            };

            imgfx_err = esp_imgfx_rotate_process(rotate_handle, &rotate_input_data, &rotate_output_data);
            if (imgfx_err != ESP_IMGFX_ERR_OK) {
                ESP_LOGE(TAG, "esp_imgfx_rotate_process failed");
                heap_caps_free(rotate_dst);
                rotate_dst = nullptr;
                if (ioctl(video_fd_, VIDIOC_QBUF, &buf) != 0) {
                    ESP_LOGE(TAG, "Cleanup: VIDIOC_QBUF failed");
                }
                esp_imgfx_rotate_close(rotate_handle);
                rotate_handle = nullptr;
                return false;
            }

            frame.data = rotate_dst;

            heap_caps_free(rotate_src);
            rotate_src = nullptr;

            esp_imgfx_rotate_close(rotate_handle);
            rotate_handle = nullptr;
#else   // CONFIG_SOC_PPA_SUPPORTED
            uint8_t* rotate_src = nullptr;

            ppa_srm_color_mode_t ppa_color_mode;
            switch (frame.format) {
                case V4L2_PIX_FMT_RGB565:
                    rotate_src = frame.data;
                    ppa_color_mode = PPA_SRM_COLOR_MODE_RGB565;
                    break;
                case V4L2_PIX_FMT_RGB24:
                    rotate_src = frame.data;
                    ppa_color_mode = PPA_SRM_COLOR_MODE_RGB888;
                    break;
                case V4L2_PIX_FMT_YUYV: {
                    ESP_LOGW(TAG, "YUYV format is not supported for PPA rotation, using software conversion to RGB888");
                    rotate_src = (uint8_t*)heap_caps_malloc(frame.width * frame.height * 3,
                                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                    if (rotate_src == nullptr) {
                        ESP_LOGE(TAG, "Failed to allocate memory for rotate image");
                        if (ioctl(video_fd_, VIDIOC_QBUF, &buf) != 0) {
                            ESP_LOGE(TAG, "Cleanup: VIDIOC_QBUF failed");
                        }
                        return false;
                    }
                    esp_imgfx_color_convert_cfg_t convert_cfg = {
                        .in_res = {.width = static_cast<int16_t>(frame.width),
                                   .height = static_cast<int16_t>(frame.height)},
                        .in_pixel_fmt = ESP_IMGFX_PIXEL_FMT_YUYV,
                        .out_pixel_fmt = ESP_IMGFX_PIXEL_FMT_RGB888,
                    };
                    esp_imgfx_color_convert_handle_t convert_handle = nullptr;
                    esp_imgfx_err_t err = esp_imgfx_color_convert_open(&convert_cfg, &convert_handle);
                    if (err != ESP_IMGFX_ERR_OK || convert_handle == nullptr) {
                        ESP_LOGE(TAG, "esp_imgfx_color_convert_open failed");
                        heap_caps_free(rotate_src);
                        rotate_src = nullptr;
                        if (ioctl(video_fd_, VIDIOC_QBUF, &buf) != 0) {
                            ESP_LOGE(TAG, "Cleanup: VIDIOC_QBUF failed");
                        }
                        return false;
                    }
                    esp_imgfx_data_t convert_input_data = {
                        .data = frame.data,
                        .data_len = frame.len,
                    };
                    esp_imgfx_data_t convert_output_data = {
                        .data = rotate_src,
                        .data_len = static_cast<uint32_t>(frame.width * frame.height * 3),
                    };
                    err = esp_imgfx_color_convert_process(convert_handle, &convert_input_data, &convert_output_data);
                    if (err != ESP_IMGFX_ERR_OK) {
                        ESP_LOGE(TAG, "esp_imgfx_color_convert_process failed");
                        heap_caps_free(rotate_src);
                        rotate_src = nullptr;
                        esp_imgfx_color_convert_close(convert_handle);
                        convert_handle = nullptr;
                        if (ioctl(video_fd_, VIDIOC_QBUF, &buf) != 0) {
                            ESP_LOGE(TAG, "Cleanup: VIDIOC_QBUF failed");
                        }
                        return false;
                    }
                    esp_imgfx_color_convert_close(convert_handle);
                    convert_handle = nullptr;
                    ppa_color_mode = PPA_SRM_COLOR_MODE_RGB888;
                    heap_caps_free(frame.data);
                    frame.data = rotate_src;
                    frame.len = frame.width * frame.height * 3;
                    break;
                }
                default:
                    ESP_LOGE(TAG, "unsupported sensor format for PPA rotation: 0x%08lx", sensor_format_);
                    if (ioctl(video_fd_, VIDIOC_QBUF, &buf) != 0) {
                        ESP_LOGE(TAG, "Cleanup: VIDIOC_QBUF failed");
                    }
                    return false;
            }

            uint8_t* rotate_dst = (uint8_t*)heap_caps_malloc(
                frame.width * frame.height * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT | MALLOC_CAP_CACHE_ALIGNED);
            if (rotate_dst == nullptr) {
                ESP_LOGE(TAG, "Failed to allocate memory for rotate image");
                if (ioctl(video_fd_, VIDIOC_QBUF, &buf) != 0) {
                    ESP_LOGE(TAG, "Cleanup: VIDIOC_QBUF failed");
                }
                return false;
            }

            ppa_client_handle_t ppa_client = nullptr;
            ppa_client_config_t client_cfg = {
                .oper_type = PPA_OPERATION_SRM,
                .max_pending_trans_num = 1,
            };
            esp_err_t err = ppa_register_client(&client_cfg, &ppa_client);
            if (err != ESP_OK || ppa_client == nullptr) {
                ESP_LOGE(TAG, "ppa_register_client failed: %d", (int)err);
                heap_caps_free(rotate_dst);
                rotate_dst = nullptr;
                if (ioctl(video_fd_, VIDIOC_QBUF, &buf) != 0) {
                    ESP_LOGE(TAG, "Cleanup: VIDIOC_QBUF failed");
                }
                return false;
            }

            ppa_srm_rotation_angle_t ppa_angle = IMAGE_ROTATION_ANGLE;

            ppa_srm_oper_config_t srm_cfg = {};
            srm_cfg.in.buffer = (void*)rotate_src;
            srm_cfg.in.pic_w = sensor_width_;
            srm_cfg.in.pic_h = sensor_height_;
            srm_cfg.in.block_w = sensor_width_;
            srm_cfg.in.block_h = sensor_height_;
            srm_cfg.in.block_offset_x = 0;
            srm_cfg.in.block_offset_y = 0;
            srm_cfg.in.srm_cm = ppa_color_mode;

            srm_cfg.out.buffer = (void*)rotate_dst;
            srm_cfg.out.buffer_size = frame.len;
            srm_cfg.out.pic_w = frame.width;
            srm_cfg.out.pic_h = frame.height;
            srm_cfg.out.block_offset_x = 0;
            srm_cfg.out.block_offset_y = 0;
            srm_cfg.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;

            // 等比例缩放 1.0
            srm_cfg.scale_x = 1.0f;
            srm_cfg.scale_y = 1.0f;
            srm_cfg.rotation_angle = ppa_angle;
            srm_cfg.mode = PPA_TRANS_MODE_BLOCKING;
            srm_cfg.user_data = nullptr;

            err = ppa_do_scale_rotate_mirror(ppa_client, &srm_cfg);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "ppa_do_scale_rotate_mirror failed: %d", (int)err);
                heap_caps_free(rotate_dst);
                rotate_dst = nullptr;
                (void)ppa_unregister_client(ppa_client);
                if (ioctl(video_fd_, VIDIOC_QBUF, &buf) != 0) {
                    ESP_LOGE(TAG, "Cleanup: VIDIOC_QBUF failed");
                }
                return false;
            }

            (void)ppa_unregister_client(ppa_client);

            frame.data = rotate_dst;
            frame.len = frame.width * frame.height * 2;
            frame.format = V4L2_PIX_FMT_RGB565;
            heap_caps_free(rotate_src);
            rotate_src = nullptr;
#endif  // CONFIG_SOC_PPA_SUPPORTED
#endif  // CONFIG_XIAOZHI_ENABLE_ROTATE_CAMERA_IMAGE
        }

        if (ioctl(video_fd_, VIDIOC_QBUF, &buf) != 0) {
            ESP_LOGE(TAG, "VIDIOC_QBUF failed");
        }
        if (valid_frames == 3) {
            break;
        }
    }
    if (valid_frames < 3) {
        ESP_LOGE(TAG, "capture failed: only got %d valid frames", valid_frames);
        return false;
    }

    cleanup_guard.keep = true;
    return true;
}

bool EspVideo::ShowPreviewFrame(const FrameBuffer& frame) {
    auto display = dynamic_cast<LvglDisplay*>(Board::GetInstance().GetDisplay());
    if (display == nullptr) {
        return true;
    }

    if (frame.data == nullptr) {
        ESP_LOGE(TAG, "frame.data is null");
        return false;
    }

    uint16_t w = frame.width;
    uint16_t h = frame.height;
    size_t lvgl_image_size = frame.len;
    size_t stride = ((w * 2) + 3) & ~3;
    lv_color_format_t color_format = LV_COLOR_FORMAT_RGB565;
    uint8_t* data = nullptr;

    switch (frame.format) {
        case V4L2_PIX_FMT_YUYV:
        case V4L2_PIX_FMT_YUV420:
        case V4L2_PIX_FMT_RGB24: {
            color_format = LV_COLOR_FORMAT_RGB565;
            data = (uint8_t*)heap_caps_malloc(w * h * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (data == nullptr) {
                ESP_LOGE(TAG, "Failed to allocate memory for preview image");
                return false;
            }
            esp_imgfx_color_convert_cfg_t convert_cfg = {
                .in_res = {.width = static_cast<int16_t>(frame.width),
                           .height = static_cast<int16_t>(frame.height)},
                .in_pixel_fmt = static_cast<esp_imgfx_pixel_fmt_t>(frame.format),
                .out_pixel_fmt = ESP_IMGFX_PIXEL_FMT_RGB565_LE,
                .color_space_std = ESP_IMGFX_COLOR_SPACE_STD_BT601,
            };
            esp_imgfx_color_convert_handle_t convert_handle = nullptr;
            esp_imgfx_err_t err = esp_imgfx_color_convert_open(&convert_cfg, &convert_handle);
            if (err != ESP_IMGFX_ERR_OK || convert_handle == nullptr) {
                ESP_LOGE(TAG, "esp_imgfx_color_convert_open failed");
                heap_caps_free(data);
                return false;
            }
            esp_imgfx_data_t convert_input_data = {
                .data = frame.data,
                .data_len = frame.len,
            };
            esp_imgfx_data_t convert_output_data = {
                .data = data,
                .data_len = static_cast<uint32_t>(w * h * 2),
            };
            err = esp_imgfx_color_convert_process(convert_handle, &convert_input_data, &convert_output_data);
            if (err != ESP_IMGFX_ERR_OK) {
                ESP_LOGE(TAG, "esp_imgfx_color_convert_process failed");
                heap_caps_free(data);
                esp_imgfx_color_convert_close(convert_handle);
                return false;
            }
            esp_imgfx_color_convert_close(convert_handle);
            lvgl_image_size = w * h * 2;
            break;
        }

        case V4L2_PIX_FMT_RGB565:
            data = (uint8_t*)heap_caps_malloc(w * h * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (data == nullptr) {
                ESP_LOGE(TAG, "Failed to allocate memory for preview image");
                return false;
            }
            memcpy(data, frame.data, frame.len);
            lvgl_image_size = frame.len;
            break;

#ifdef CONFIG_XIAOZHI_CAMERA_ALLOW_JPEG_INPUT
        case V4L2_PIX_FMT_JPEG: {
            uint8_t* out_data = nullptr;
            size_t out_len = 0;
            size_t out_width = 0;
            size_t out_height = 0;
            size_t out_stride = 0;

            esp_err_t ret =
                jpeg_to_image(frame.data, frame.len, &out_data, &out_len, &out_width, &out_height, &out_stride);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to decode JPEG image: %d (%s)", (int)ret, esp_err_to_name(ret));
                if (out_data != nullptr) {
                    heap_caps_free(out_data);
                }
                return false;
            }

            data = out_data;
            w = out_width;
            h = out_height;
            lvgl_image_size = out_len;
            stride = out_stride;
            break;
        }
#endif
        default:
            ESP_LOGE(TAG, "unsupported frame format: 0x%08lx", frame.format);
            return false;
    }

    auto image = std::make_unique<LvglAllocatedImage>(data, lvgl_image_size, w, h, stride, color_format);
    display->SetPreviewImage(std::move(image));
    return true;
}

bool EspVideo::EncodeFrameToJpeg(const FrameBuffer& frame, std::string& jpeg_data, uint8_t quality) const {
    if (frame.data == nullptr || frame.len == 0) {
        return false;
    }

    uint8_t* jpeg_buffer = nullptr;
    size_t jpeg_len = 0;
    if (!image_to_jpeg(frame.data, frame.len, frame.width, frame.height, frame.format, quality, &jpeg_buffer, &jpeg_len)) {
        return false;
    }

    jpeg_data.assign(reinterpret_cast<const char*>(jpeg_buffer), jpeg_len);
    free(jpeg_buffer);
    return true;
}

bool EspVideo::Capture() {
    FrameBuffer captured;
    FrameBuffer preview_frame;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!CaptureFrameLocked(captured)) {
            return false;
        }

        if (!CloneFrameBuffer(captured, preview_frame)) {
            ESP_LOGW(TAG, "Failed to clone frame for on-device preview");
        }

        FreeFrameBuffer(frame_);
        frame_ = captured;
        captured.data = nullptr;
        captured.len = 0;
        captured.format = 0;
    }

    if (preview_frame.data != nullptr) {
        if (!ShowPreviewFrame(preview_frame)) {
            ESP_LOGW(TAG, "Failed to show camera preview on device display");
        }
        FreeFrameBuffer(preview_frame);
    }

    return true;
}

bool EspVideo::CaptureToRgb565(std::vector<uint16_t>& rgb565_data, uint16_t& width, uint16_t& height) {
    FrameBuffer snapshot;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!CaptureFrameLocked(snapshot)) {
            return false;
        }
    }

    width = snapshot.width;
    height = snapshot.height;

    const size_t pixel_count = static_cast<size_t>(width) * height;
    const size_t rgb565_bytes = pixel_count * sizeof(uint16_t);
    bool ok = false;

    rgb565_data.resize(pixel_count);
    switch (snapshot.format) {
        case V4L2_PIX_FMT_RGB565:
            memcpy(rgb565_data.data(), snapshot.data, MIN(snapshot.len, rgb565_bytes));
            ok = true;
            break;

        case V4L2_PIX_FMT_YUYV:
        case V4L2_PIX_FMT_YUV420:
        case V4L2_PIX_FMT_RGB24: {
            esp_imgfx_color_convert_cfg_t convert_cfg = {
                .in_res = {
                    .width = static_cast<int16_t>(snapshot.width),
                    .height = static_cast<int16_t>(snapshot.height),
                },
                .in_pixel_fmt = static_cast<esp_imgfx_pixel_fmt_t>(snapshot.format),
                .out_pixel_fmt = ESP_IMGFX_PIXEL_FMT_RGB565_LE,
                .color_space_std = ESP_IMGFX_COLOR_SPACE_STD_BT601,
            };
            esp_imgfx_color_convert_handle_t convert_handle = nullptr;
            esp_imgfx_err_t err = esp_imgfx_color_convert_open(&convert_cfg, &convert_handle);
            if (err != ESP_IMGFX_ERR_OK || convert_handle == nullptr) {
                ESP_LOGE(TAG, "esp_imgfx_color_convert_open failed");
                break;
            }

            esp_imgfx_data_t convert_input_data = {
                .data = snapshot.data,
                .data_len = snapshot.len,
            };
            esp_imgfx_data_t convert_output_data = {
                .data = reinterpret_cast<uint8_t*>(rgb565_data.data()),
                .data_len = static_cast<uint32_t>(rgb565_bytes),
            };
            err = esp_imgfx_color_convert_process(convert_handle, &convert_input_data, &convert_output_data);
            esp_imgfx_color_convert_close(convert_handle);
            if (err != ESP_IMGFX_ERR_OK) {
                ESP_LOGE(TAG, "esp_imgfx_color_convert_process failed");
                break;
            }
            ok = true;
            break;
        }

        default:
            ESP_LOGW(TAG, "CaptureToRgb565 does not support frame format: 0x%08lx", snapshot.format);
            break;
    }

    FreeFrameBuffer(snapshot);
    return ok;
}

bool EspVideo::CaptureToJpeg(std::string& jpeg_data, uint8_t quality) {
    FrameBuffer snapshot;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!CaptureFrameLocked(snapshot)) {
            return false;
        }
    }

    bool ok = EncodeFrameToJpeg(snapshot, jpeg_data, quality);
    FreeFrameBuffer(snapshot);
    return ok;
}

bool EspVideo::SetHMirror(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (video_fd_ < 0) {
        return false;
    }
    struct v4l2_ext_controls ctrls = {};
    struct v4l2_ext_control ctrl = {};
    ctrl.id = V4L2_CID_HFLIP;
    ctrl.value = enabled ? 1 : 0;
    ctrls.ctrl_class = V4L2_CTRL_CLASS_USER;
    ctrls.count = 1;
    ctrls.controls = &ctrl;
    if (ioctl(video_fd_, VIDIOC_S_EXT_CTRLS, &ctrls) != 0) {
        ESP_LOGE(TAG, "set HFLIP failed");
        return false;
    }
    return true;
}

bool EspVideo::SetVFlip(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (video_fd_ < 0) {
        return false;
    }
    struct v4l2_ext_controls ctrls = {};
    struct v4l2_ext_control ctrl = {};
    ctrl.id = V4L2_CID_VFLIP;
    ctrl.value = enabled ? 1 : 0;
    ctrls.ctrl_class = V4L2_CTRL_CLASS_USER;
    ctrls.count = 1;
    ctrls.controls = &ctrl;
    if (ioctl(video_fd_, VIDIOC_S_EXT_CTRLS, &ctrls) != 0) {
        ESP_LOGE(TAG, "set VFLIP failed");
        return false;
    }
    return true;
}

/**
 * @brief 将摄像头捕获的图像发送到远程服务器进行AI分析和解释
 *
 * 该函数将当前摄像头缓冲区中的图像编码为JPEG格式，并通过HTTP POST请求
 * 以multipart/form-data的形式发送到指定的解释服务器。服务器将根据提供的
 * 问题对图像进行AI分析并返回结果。
 *
 * 实现特点：
 * - Clone the last MCP-captured frame before encoding so preview streaming cannot overwrite it
 * - Upload the encoded JPEG with multipart/form-data
 * - Support device ID, client ID and bearer token headers
 *
 * @param question 要向AI提出的关于图像的问题，将作为表单字段发送
 * @return std::string 服务器返回的JSON格式响应字符串
 *         成功时包含AI分析结果，失败时包含错误信息
 *         格式示例：{"success": true, "result": "分析结果"}
 *                  {"success": false, "message": "错误信息"}
 *
 * @note 调用此函数前必须先调用SetExplainUrl()设置服务器URL
 * @warning 如果摄像头缓冲区为空或网络连接失败，将返回错误信息
 */
std::string EspVideo::Explain(const std::string& question) {
    if (explain_url_.empty()) {
        throw std::runtime_error("Image explain URL or token is not set");
    }

    FrameBuffer snapshot;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (frame_.data == nullptr || frame_.len == 0) {
            throw std::runtime_error("No camera frame captured");
        }
        if (!CloneFrameBuffer(frame_, snapshot)) {
            throw std::runtime_error("Failed to clone camera frame");
        }
    }

    size_t raw_frame_size = snapshot.len;
    std::string jpeg_data;
    if (!EncodeFrameToJpeg(snapshot, jpeg_data, 80)) {
        FreeFrameBuffer(snapshot);
        throw std::runtime_error("Failed to encode image to JPEG");
    }
    FreeFrameBuffer(snapshot);

    auto network = Board::GetInstance().GetNetwork();
    auto http = network->CreateHttp(3);
    std::string boundary = "----ESP32_CAMERA_BOUNDARY";

    http->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
    http->SetHeader("Client-Id", Board::GetInstance().GetUuid().c_str());
    if (!explain_token_.empty()) {
        http->SetHeader("Authorization", "Bearer " + explain_token_);
    }
    http->SetHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
    http->SetHeader("Transfer-Encoding", "chunked");
    if (!http->Open("POST", explain_url_)) {
        ESP_LOGE(TAG, "Failed to connect to explain URL");
        throw std::runtime_error("Failed to connect to explain URL");
    }

    {
        std::string question_field;
        question_field += "--" + boundary + "\r\n";
        question_field += "Content-Disposition: form-data; name=\"question\"\r\n";
        question_field += "\r\n";
        question_field += question + "\r\n";
        http->Write(question_field.c_str(), question_field.size());
    }
    {
        std::string file_header;
        file_header += "--" + boundary + "\r\n";
        file_header += "Content-Disposition: form-data; name=\"file\"; filename=\"camera.jpg\"\r\n";
        file_header += "Content-Type: image/jpeg\r\n";
        file_header += "\r\n";
        http->Write(file_header.c_str(), file_header.size());
    }

    http->Write(jpeg_data.c_str(), jpeg_data.size());

    {
        std::string multipart_footer;
        multipart_footer += "\r\n--" + boundary + "--\r\n";
        http->Write(multipart_footer.c_str(), multipart_footer.size());
    }
    http->Write("", 0);

    if (http->GetStatusCode() != 200) {
        ESP_LOGE(TAG, "Failed to upload photo, status code: %d", http->GetStatusCode());
        http->Close();
        throw std::runtime_error("Failed to upload photo");
    }

    std::string result = http->ReadAll();
    http->Close();

    size_t remain_stack_size = uxTaskGetStackHighWaterMark(nullptr);
    ESP_LOGI(TAG, "Explain image size=%d bytes, compressed size=%d, remain stack size=%d, question=%s\n%s",
             (int)raw_frame_size, (int)jpeg_data.size(), (int)remain_stack_size, question.c_str(), result.c_str());
    return result;
}
