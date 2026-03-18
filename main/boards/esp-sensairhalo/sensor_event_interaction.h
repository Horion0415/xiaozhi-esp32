#ifndef SENSOR_EVENT_INTERACTION_H_
#define SENSOR_EVENT_INTERACTION_H_

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string_view>

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>

#include "bsp/esp_sensair_halo.h"

class SensorEventInteraction {
public:
    SensorEventInteraction();
    ~SensorEventInteraction();

private:
    static void TouchTaskEntry(void* arg);
    static void MotionTaskEntry(void* arg);

    void InitializeTouchButton();
    void InitializeImu();
    void InitializeMagnetometer();
    void TouchTaskLoop();
    void MotionTaskLoop();
    void ProcessMagnetometerSample();
    bool ShouldTrigger(int64_t& last_trigger_us, int cooldown_ms);
    void TriggerInteraction(std::string_view event_text);

    std::atomic<bool> stop_requested_{false};
    touch_ic_bs8112a3_handle_t touch_button_handle_ = nullptr;
    EventGroupHandle_t touch_event_group_ = nullptr;
    bmi270_handle_t imu_handle_ = nullptr;
    bmm150_aux_handle_t mag_handle_ = {};
    bool mag_initialized_ = false;
    TaskHandle_t touch_task_handle_ = nullptr;
    TaskHandle_t motion_task_handle_ = nullptr;
    std::mutex trigger_mutex_;
    int64_t last_ear_trigger_us_ = 0;
    int64_t last_motion_trigger_us_ = 0;
    int64_t last_magnetic_anomaly_trigger_us_ = 0;
    int64_t last_mag_poll_us_ = 0;
    int64_t magnetic_anomaly_since_us_ = 0;
    bool magnetic_anomaly_reported_ = false;
};

#endif // SENSOR_EVENT_INTERACTION_H_
