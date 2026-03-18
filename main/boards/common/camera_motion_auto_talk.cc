#include "camera_motion_auto_talk.h"

#include "sdkconfig.h"

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <inttypes.h>

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>

#include "application.h"
#include "esp_video.h"
#include "mcp_server.h"
#include "settings.h"

namespace {

constexpr char TAG[] = "MotionAutoTalk";
constexpr char kSettingsNamespace[] = "cam_motion";
constexpr char kWakeWord[] = "<detect>motion detected</detect>";

constexpr uint32_t kMotionBlockSize = 4;
constexpr uint32_t kMotionBlockHitPixels = 5;
constexpr int kMotionBoxPadding = 2;

#if defined(CONFIG_XIAOZHI_SENSAIRHALO_MOTION_AUTO_TALK_DEFAULT_ENABLED)
constexpr bool kMotionAutoTalkDefaultEnabled = true;
#else
constexpr bool kMotionAutoTalkDefaultEnabled = false;
#endif

static inline uint8_t rgb565_to_luma8(uint16_t pixel) {
    uint8_t g6 = static_cast<uint8_t>((pixel >> 5) & 0x3F);
    return static_cast<uint8_t>((g6 << 2) | (g6 >> 4));
}

const char* RuntimeStateName(CameraMotionAutoTalk::RuntimeState state) {
    switch (state) {
        case CameraMotionAutoTalk::RuntimeState::Monitoring:
            return "monitoring";
        case CameraMotionAutoTalk::RuntimeState::Cooldown:
            return "cooldown";
    }
    return "unknown";
}

}  // namespace

CameraMotionAutoTalk::CameraMotionAutoTalk(EspVideo* camera) : camera_(camera) {
    LoadSettings();
    if (camera_ != nullptr) {
        xTaskCreate(&CameraMotionAutoTalk::TaskEntry, "cam_motion", 6144, this, 2, &task_handle_);
    }
}

CameraMotionAutoTalk::~CameraMotionAutoTalk() {
    stop_requested_ = true;
}

std::string CameraMotionAutoTalk::SetEnabled(bool enabled) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        config_.enabled = enabled;
        status_.enabled = enabled;
        status_.motion_detected = false;
        status_.active_pixels = 0;
        status_.box = MotionBox{};
        status_.has_prev_frame = false;
        status_.state = RuntimeState::Monitoring;
        status_.cooldown_started_us = 0;
        positive_frames_ = 0;
        hold_frames_left_ = 0;
        alert_active_ = false;
        prev_luma_.clear();
        block_counts_.clear();
    }
    SaveEnabled(enabled);
    ESP_LOGI(TAG, "Motion auto-talk %s", enabled ? "enabled" : "disabled");
    return GetConfigJson();
}

void CameraMotionAutoTalk::InitializeMcpTools() {
    auto& mcp_server = McpServer::GetInstance();

    mcp_server.AddTool("self.camera.motion_auto_talk.enable",
        "Enable or disable camera motion detection / motion-triggered auto-talk. "
        "Use this when the user says to turn on/off motion detection, stop motion detection, "
        "close action detection, or disable/enable automatic motion-triggered conversation. "
        "Chinese hints: 动作检测, 关闭动作检测, 开启动作检测, 关闭摄像头动作检测.",
        PropertyList({
            Property("enable", kPropertyTypeInteger, 0, 1)
        }),
        [this](const PropertyList& properties) -> ReturnValue {
            return SetEnabled(properties["enable"].value<int>() != 0);
        });

    mcp_server.AddTool("self.camera.motion_detection.enable",
        "Enable or disable camera motion detection. "
        "This is an alias of motion auto-talk control and should be used for natural language requests "
        "such as turn off motion detection / stop motion detection / close action detection / 关闭动作检测.",
        PropertyList({
            Property("enable", kPropertyTypeInteger, 0, 1)
        }),
        [this](const PropertyList& properties) -> ReturnValue {
            return SetEnabled(properties["enable"].value<int>() != 0);
        });

    mcp_server.AddTool("self.camera.motion_auto_talk.get_config",
        "Get the current camera motion detection configuration, including motion-triggered auto-talk thresholds.",
        PropertyList(),
        [this](const PropertyList&) -> ReturnValue {
            return GetConfigJson();
        });

    mcp_server.AddTool("self.camera.motion_detection.get_config",
        "Get the current camera motion detection configuration.",
        PropertyList(),
        [this](const PropertyList&) -> ReturnValue {
            return GetConfigJson();
        });

    mcp_server.AddTool("self.camera.motion_auto_talk.set_config",
        "Update camera motion detection thresholds and timing. Omitted fields keep their current values.",
        PropertyList({
            Property("diff_threshold", kPropertyTypeInteger, -1, -1, 255),
            Property("active_pixel_percent", kPropertyTypeInteger, -1, -1, 100),
            Property("confirm_frames", kPropertyTypeInteger, -1, -1, 20),
            Property("hold_frames", kPropertyTypeInteger, -1, -1, 20),
            Property("cooldown_sec", kPropertyTypeInteger, -1, -1, 3600),
        }),
        [this](const PropertyList& properties) -> ReturnValue {
            Settings settings(kSettingsNamespace, true);

            try {
                int value = properties["diff_threshold"].value<int>();
                if (value >= 0) {
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        config_.diff_threshold = value;
                    }
                    settings.SetInt("diff_thr", value);
                    ESP_LOGI(TAG, "Set diff_threshold to %d", value);
                }
            } catch (const std::runtime_error&) {
            }

            try {
                int value = properties["active_pixel_percent"].value<int>();
                if (value >= 0) {
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        config_.active_pixel_percent = value;
                    }
                    settings.SetInt("active_pct", value);
                    ESP_LOGI(TAG, "Set active_pixel_percent to %d", value);
                }
            } catch (const std::runtime_error&) {
            }

            try {
                int value = properties["confirm_frames"].value<int>();
                if (value >= 0) {
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        config_.confirm_frames = value;
                    }
                    settings.SetInt("confirm", value);
                    ESP_LOGI(TAG, "Set confirm_frames to %d", value);
                }
            } catch (const std::runtime_error&) {
            }

            try {
                int value = properties["hold_frames"].value<int>();
                if (value >= 0) {
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        config_.hold_frames = value;
                    }
                    settings.SetInt("hold", value);
                    ESP_LOGI(TAG, "Set hold_frames to %d", value);
                }
            } catch (const std::runtime_error&) {
            }

            try {
                int value = properties["cooldown_sec"].value<int>();
                if (value >= 0) {
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        config_.cooldown_sec = value;
                    }
                    settings.SetInt("cooldown", value);
                    ESP_LOGI(TAG, "Set cooldown_sec to %d", value);
                }
            } catch (const std::runtime_error&) {
            }

            return GetConfigJson();
        });

    mcp_server.AddTool("self.camera.motion_detection.set_config",
        "Update camera motion detection thresholds and timing.",
        PropertyList({
            Property("diff_threshold", kPropertyTypeInteger, -1, -1, 255),
            Property("active_pixel_percent", kPropertyTypeInteger, -1, -1, 100),
            Property("confirm_frames", kPropertyTypeInteger, -1, -1, 20),
            Property("hold_frames", kPropertyTypeInteger, -1, -1, 20),
            Property("cooldown_sec", kPropertyTypeInteger, -1, -1, 3600),
        }),
        [this](const PropertyList& properties) -> ReturnValue {
            Settings settings(kSettingsNamespace, true);

            try {
                int value = properties["diff_threshold"].value<int>();
                if (value >= 0) {
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        config_.diff_threshold = value;
                    }
                    settings.SetInt("diff_thr", value);
                    ESP_LOGI(TAG, "Set diff_threshold to %d", value);
                }
            } catch (const std::runtime_error&) {
            }

            try {
                int value = properties["active_pixel_percent"].value<int>();
                if (value >= 0) {
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        config_.active_pixel_percent = value;
                    }
                    settings.SetInt("active_pct", value);
                    ESP_LOGI(TAG, "Set active_pixel_percent to %d", value);
                }
            } catch (const std::runtime_error&) {
            }

            try {
                int value = properties["confirm_frames"].value<int>();
                if (value >= 0) {
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        config_.confirm_frames = value;
                    }
                    settings.SetInt("confirm", value);
                    ESP_LOGI(TAG, "Set confirm_frames to %d", value);
                }
            } catch (const std::runtime_error&) {
            }

            try {
                int value = properties["hold_frames"].value<int>();
                if (value >= 0) {
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        config_.hold_frames = value;
                    }
                    settings.SetInt("hold", value);
                    ESP_LOGI(TAG, "Set hold_frames to %d", value);
                }
            } catch (const std::runtime_error&) {
            }

            try {
                int value = properties["cooldown_sec"].value<int>();
                if (value >= 0) {
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        config_.cooldown_sec = value;
                    }
                    settings.SetInt("cooldown", value);
                    ESP_LOGI(TAG, "Set cooldown_sec to %d", value);
                }
            } catch (const std::runtime_error&) {
            }

            return GetConfigJson();
        });

    mcp_server.AddTool("self.camera.motion_auto_talk.get_status",
        "Get the latest runtime status of camera motion detection / motion-triggered auto-talk.",
        PropertyList(),
        [this](const PropertyList&) -> ReturnValue {
            return GetStatusJson();
        });

    mcp_server.AddTool("self.camera.motion_detection.get_status",
        "Get the latest runtime status of camera motion detection.",
        PropertyList(),
        [this](const PropertyList&) -> ReturnValue {
            return GetStatusJson();
        });
}

void CameraMotionAutoTalk::TaskEntry(void* arg) {
    static_cast<CameraMotionAutoTalk*>(arg)->TaskLoop();
}

void CameraMotionAutoTalk::TaskLoop() {
    std::vector<uint16_t> frame;

    while (!stop_requested_) {
        Config config;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            config = config_;
        }

        if (!config.enabled || Application::GetInstance().GetDeviceState() != kDeviceStateIdle) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                status_.enabled = config.enabled;
                status_.motion_detected = false;
                status_.active_pixels = 0;
                status_.box = MotionBox{};
                status_.has_prev_frame = false;
                positive_frames_ = 0;
                hold_frames_left_ = 0;
                alert_active_ = false;
                prev_luma_.clear();
                block_counts_.clear();
                if (!config.enabled) {
                    status_.state = RuntimeState::Monitoring;
                    status_.cooldown_started_us = 0;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        uint16_t width = 0;
        uint16_t height = 0;
        if (!camera_->CaptureToRgb565(frame, width, height)) {
            ESP_LOGD(TAG, "Failed to capture frame for motion detection");
            vTaskDelay(pdMS_TO_TICKS(config.sample_interval_ms));
            continue;
        }

        DetectionResult detection = DetectMotion(frame, width, height);
        bool activated = false;
        bool motion_alert = UpdateAlertState(detection.detected, activated);

        bool should_trigger = false;
        int64_t now_us = esp_timer_get_time();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            status_.enabled = config_.enabled;
            status_.has_prev_frame = !prev_luma_.empty();
            status_.motion_detected = motion_alert;
            status_.active_pixels = detection.active_pixels;
            status_.box = detection.box;

            if (status_.state == RuntimeState::Cooldown) {
                int64_t cooldown_us = static_cast<int64_t>(config_.cooldown_sec) * 1000000LL;
                if ((now_us - status_.cooldown_started_us) >= cooldown_us && !motion_alert) {
                    status_.state = RuntimeState::Monitoring;
                    ESP_LOGI(TAG, "Motion auto-talk rearmed");
                }
            }

            if (config_.enabled &&
                Application::GetInstance().GetDeviceState() == kDeviceStateIdle &&
                status_.state == RuntimeState::Monitoring &&
                activated) {
                status_.state = RuntimeState::Cooldown;
                status_.cooldown_started_us = now_us;
                status_.last_trigger_us = now_us;
                should_trigger = true;
            }
        }

        if (should_trigger) {
            ESP_LOGI(TAG, "Motion triggered auto-talk: active_pixels=%u", detection.active_pixels);
            Application::GetInstance().WakeWordInvoke(kWakeWord);
        }

        vTaskDelay(pdMS_TO_TICKS(config.sample_interval_ms));
    }

    task_handle_ = nullptr;
    vTaskDelete(nullptr);
}

void CameraMotionAutoTalk::LoadSettings() {
    Settings settings(kSettingsNamespace, false);

    std::lock_guard<std::mutex> lock(mutex_);
    config_.enabled = settings.GetBool("enable", kMotionAutoTalkDefaultEnabled);
    config_.diff_threshold = settings.GetInt("diff_thr", CONFIG_XIAOZHI_SENSAIRHALO_MOTION_AUTO_TALK_DIFF_THRESHOLD);
    config_.active_pixel_percent = settings.GetInt("active_pct", CONFIG_XIAOZHI_SENSAIRHALO_MOTION_AUTO_TALK_ACTIVE_PIXEL_PERCENT);
    config_.confirm_frames = settings.GetInt("confirm", CONFIG_XIAOZHI_SENSAIRHALO_MOTION_AUTO_TALK_CONFIRM_FRAMES);
    config_.hold_frames = settings.GetInt("hold", CONFIG_XIAOZHI_SENSAIRHALO_MOTION_AUTO_TALK_HOLD_FRAMES);
    config_.cooldown_sec = settings.GetInt("cooldown", CONFIG_XIAOZHI_SENSAIRHALO_MOTION_AUTO_TALK_COOLDOWN_SEC);
    config_.sample_interval_ms = settings.GetInt("sample_ms", CONFIG_XIAOZHI_SENSAIRHALO_MOTION_AUTO_TALK_SAMPLE_INTERVAL_MS);
    status_.enabled = config_.enabled;
}

void CameraMotionAutoTalk::SaveEnabled(bool enabled) {
    Settings settings(kSettingsNamespace, true);
    settings.SetBool("enable", enabled);
}

CameraMotionAutoTalk::DetectionResult CameraMotionAutoTalk::DetectMotion(const std::vector<uint16_t>& frame,
                                                                         uint16_t width, uint16_t height) {
    DetectionResult result;
    if (frame.empty() || width == 0 || height == 0) {
        return result;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    Config config = config_;

    const uint32_t roi_width = std::min<uint16_t>(width, height);
    const uint32_t roi_height = roi_width;
    const uint32_t roi_x = (static_cast<uint32_t>(width) - roi_width) / 2U;
    const uint32_t roi_y = (static_cast<uint32_t>(height) - roi_height) / 2U;
    const uint32_t roi_size = roi_width * roi_height;
    const uint32_t blocks_x = (roi_width + kMotionBlockSize - 1U) / kMotionBlockSize;
    const uint32_t blocks_y = (roi_height + kMotionBlockSize - 1U) / kMotionBlockSize;
    const uint32_t block_count = blocks_x * blocks_y;

    if (prev_luma_.size() != roi_size) {
        prev_luma_.resize(roi_size);
        for (uint32_t y = 0; y < roi_height; y++) {
            uint32_t frame_row = (roi_y + y) * width + roi_x;
            uint32_t roi_row = y * roi_width;
            for (uint32_t x = 0; x < roi_width; x++) {
                prev_luma_[roi_row + x] = rgb565_to_luma8(frame[frame_row + x]);
            }
        }
        block_counts_.assign(block_count, 0);
        return result;
    }

    if (block_counts_.size() != block_count) {
        block_counts_.assign(block_count, 0);
    } else {
        std::fill(block_counts_.begin(), block_counts_.end(), 0);
    }

    uint32_t threshold = (roi_size * static_cast<uint32_t>(config.active_pixel_percent)) / 100U;
    if (threshold == 0) {
        threshold = 1;
    }

    int x1 = static_cast<int>(roi_x + roi_width - 1U);
    int y1 = static_cast<int>(roi_y + roi_height - 1U);
    int x2 = static_cast<int>(roi_x);
    int y2 = static_cast<int>(roi_y);

    for (uint32_t y = 0; y < roi_height; y++) {
        uint32_t frame_row = (roi_y + y) * width + roi_x;
        uint32_t roi_row = y * roi_width;
        uint32_t block_row = (y / kMotionBlockSize) * blocks_x;
        for (uint32_t x = 0; x < roi_width; x++) {
            uint32_t roi_index = roi_row + x;
            uint8_t luma = rgb565_to_luma8(frame[frame_row + x]);
            int diff = std::abs(static_cast<int>(luma) - static_cast<int>(prev_luma_[roi_index]));
            prev_luma_[roi_index] = luma;

            if (diff > config.diff_threshold) {
                uint32_t block_index = block_row + (x / kMotionBlockSize);
                if (block_counts_[block_index] < UINT8_MAX) {
                    block_counts_[block_index]++;
                }
            }
        }
    }

    for (uint32_t by = 0; by < blocks_y; by++) {
        uint32_t block_y = by * kMotionBlockSize;
        uint32_t block_h = std::min<uint32_t>(kMotionBlockSize, roi_height - block_y);

        for (uint32_t bx = 0; bx < blocks_x; bx++) {
            uint32_t block_x = bx * kMotionBlockSize;
            uint32_t block_w = std::min<uint32_t>(kMotionBlockSize, roi_width - block_x);

            if (block_counts_[by * blocks_x + bx] < kMotionBlockHitPixels) {
                continue;
            }

            result.active_pixels += block_w * block_h;
            result.box.valid = true;

            int block_x1 = static_cast<int>(roi_x + block_x);
            int block_y1 = static_cast<int>(roi_y + block_y);
            int block_x2 = block_x1 + static_cast<int>(block_w) - 1;
            int block_y2 = block_y1 + static_cast<int>(block_h) - 1;

            if (block_x1 < x1) x1 = block_x1;
            if (block_y1 < y1) y1 = block_y1;
            if (block_x2 > x2) x2 = block_x2;
            if (block_y2 > y2) y2 = block_y2;
        }
    }

    result.detected = result.active_pixels > threshold;
    if (!result.detected || !result.box.valid) {
        result.box = MotionBox{};
        return result;
    }

    result.box.x1 = std::max<int>(static_cast<int>(roi_x), x1 - kMotionBoxPadding);
    result.box.y1 = std::max<int>(static_cast<int>(roi_y), y1 - kMotionBoxPadding);
    result.box.x2 = std::min<int>(static_cast<int>(roi_x + roi_width) - 1, x2 + kMotionBoxPadding);
    result.box.y2 = std::min<int>(static_cast<int>(roi_y + roi_height) - 1, y2 + kMotionBoxPadding);
    return result;
}

bool CameraMotionAutoTalk::UpdateAlertState(bool motion_detected, bool& activated) {
    activated = false;
    bool alert_active = false;

    std::lock_guard<std::mutex> lock(mutex_);
    Config config = config_;

    if (motion_detected) {
        if (positive_frames_ < static_cast<uint32_t>(config.confirm_frames)) {
            positive_frames_++;
        }
        if (positive_frames_ >= static_cast<uint32_t>(config.confirm_frames)) {
            hold_frames_left_ = static_cast<uint32_t>(config.hold_frames);
            alert_active = true;
        }
    } else {
        positive_frames_ = 0;
        if (hold_frames_left_ > 0) {
            hold_frames_left_--;
            alert_active = true;
        }
    }

    activated = alert_active && !alert_active_;
    alert_active_ = alert_active;
    return alert_active;
}

std::string CameraMotionAutoTalk::GetConfigJson() {
    Config config;
    StatusSnapshot status;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        config = config_;
        status = status_;
    }

    char buffer[256];
    snprintf(buffer, sizeof(buffer),
             "{\"enable\":%s,\"diff_threshold\":%d,\"active_pixel_percent\":%d,"
             "\"confirm_frames\":%d,\"hold_frames\":%d,\"cooldown_sec\":%d,"
             "\"sample_interval_ms\":%d,\"state\":\"%s\"}",
             config.enabled ? "true" : "false",
             config.diff_threshold,
             config.active_pixel_percent,
             config.confirm_frames,
             config.hold_frames,
             config.cooldown_sec,
             config.sample_interval_ms,
             RuntimeStateName(status.state));
    return std::string(buffer);
}

std::string CameraMotionAutoTalk::GetStatusJson() {
    StatusSnapshot status;
    Config config;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        status = status_;
        config = config_;
    }

    int64_t now_us = esp_timer_get_time();
    int64_t cooldown_remaining_ms = 0;
    if (status.state == RuntimeState::Cooldown && status.cooldown_started_us > 0) {
        int64_t cooldown_us = static_cast<int64_t>(config.cooldown_sec) * 1000000LL;
        int64_t remain_us = cooldown_us - (now_us - status.cooldown_started_us);
        if (remain_us > 0) {
            cooldown_remaining_ms = remain_us / 1000;
        }
    }

    int64_t last_trigger_ms_ago = -1;
    if (status.last_trigger_us > 0) {
        last_trigger_ms_ago = (now_us - status.last_trigger_us) / 1000;
    }

    char buffer[320];
    snprintf(buffer, sizeof(buffer),
             "{\"enable\":%s,\"state\":\"%s\",\"motion_detected\":%s,\"has_prev_frame\":%s,"
             "\"active_pixels\":%" PRIu32 ",\"cooldown_remaining_ms\":%lld,\"last_trigger_ms_ago\":%lld,"
             "\"box\":{\"valid\":%s,\"x1\":%d,\"y1\":%d,\"x2\":%d,\"y2\":%d}}",
             status.enabled ? "true" : "false",
             RuntimeStateName(status.state),
             status.motion_detected ? "true" : "false",
             status.has_prev_frame ? "true" : "false",
             status.active_pixels,
             static_cast<long long>(cooldown_remaining_ms),
             static_cast<long long>(last_trigger_ms_ago),
             status.box.valid ? "true" : "false",
             status.box.x1,
             status.box.y1,
             status.box.x2,
             status.box.y2);
    return std::string(buffer);
}
