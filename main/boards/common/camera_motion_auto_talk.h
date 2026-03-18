#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

class EspVideo;

class CameraMotionAutoTalk {
public:
    enum class RuntimeState {
        Monitoring,
        Cooldown,
    };

    explicit CameraMotionAutoTalk(EspVideo* camera);
    ~CameraMotionAutoTalk();

    void InitializeMcpTools();

private:
    struct MotionBox {
        bool valid = false;
        int x1 = 0;
        int y1 = 0;
        int x2 = 0;
        int y2 = 0;
    };

    struct DetectionResult {
        bool detected = false;
        uint32_t active_pixels = 0;
        MotionBox box;
    };

    struct Config {
        bool enabled = false;
        int diff_threshold = 24;
        int active_pixel_percent = 5;
        int confirm_frames = 2;
        int hold_frames = 3;
        int cooldown_sec = 8;
        int sample_interval_ms = 400;
    };

    struct StatusSnapshot {
        bool enabled = false;
        bool motion_detected = false;
        bool has_prev_frame = false;
        uint32_t active_pixels = 0;
        MotionBox box;
        RuntimeState state = RuntimeState::Monitoring;
        int64_t last_trigger_us = 0;
        int64_t cooldown_started_us = 0;
    };

    static void TaskEntry(void* arg);

    void TaskLoop();
    void LoadSettings();
    void SaveEnabled(bool enabled);
    std::string SetEnabled(bool enabled);

    DetectionResult DetectMotion(const std::vector<uint16_t>& frame, uint16_t width, uint16_t height);
    bool UpdateAlertState(bool motion_detected, bool& activated);
    std::string GetConfigJson();
    std::string GetStatusJson();

    EspVideo* camera_ = nullptr;
    std::mutex mutex_;
    TaskHandle_t task_handle_ = nullptr;
    bool stop_requested_ = false;
    Config config_;
    StatusSnapshot status_;
    std::vector<uint8_t> prev_luma_;
    std::vector<uint8_t> block_counts_;
    uint32_t positive_frames_ = 0;
    uint32_t hold_frames_left_ = 0;
    bool alert_active_ = false;
};
