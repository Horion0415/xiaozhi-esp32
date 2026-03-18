#include "sensor_event_interaction.h"

#include <esp_log.h>
#include <esp_timer.h>

#include "application.h"

namespace {

constexpr char TAG[] = "SensorInteraction";
constexpr uint8_t kEarKeyIndex = 5;  // KEY6 maps to index 5 in the BS8112A3 driver.
constexpr uint16_t kEarKeyMask = static_cast<uint16_t>(1U << kEarKeyIndex);
constexpr int kEarCooldownMs = 3000;
constexpr int kMotionCooldownMs = 8000;
constexpr int kMagneticAnomalyCooldownMs = 12000;
constexpr int kMagneticAnomalyStabilityMs = 1500;
constexpr int kTouchReleasePollMs = 40;
constexpr int kMotionPollMs = 250;
constexpr int kMagPollMs = 500;
constexpr char kEarEventText[] = "<detect>someone touched the puppy's ear</detect>";
constexpr char kMotionEventText[] = "<detect>someone is shaking the puppy device</detect>";
constexpr char kMagneticAnomalyEventText[] = "<detect>the puppy sensed an unusual magnetic field nearby</detect>";

}  // namespace

SensorEventInteraction::SensorEventInteraction() {
    InitializeTouchButton();
    InitializeImu();
}

SensorEventInteraction::~SensorEventInteraction() {
    stop_requested_.store(true);
}

void SensorEventInteraction::InitializeTouchButton() {
    esp_err_t ret = bsp_touch_button_init(&touch_button_handle_);
    if (ret != ESP_OK || touch_button_handle_ == nullptr) {
        ESP_LOGW(TAG, "Failed to initialize touch button: %s", esp_err_to_name(ret));
        return;
    }

    touch_event_group_ = touch_ic_bs8112a3_get_event_group(touch_button_handle_);
    if (touch_event_group_ == nullptr) {
        ESP_LOGW(TAG, "Touch button interrupt event group is unavailable");
        return;
    }

    xTaskCreate(&SensorEventInteraction::TouchTaskEntry, "halo_touch_evt", 4 * 1024, this, 5, &touch_task_handle_);
}

void SensorEventInteraction::InitializeImu() {
    esp_err_t ret = bsp_imu_init(&imu_handle_);
    if (ret != ESP_OK || imu_handle_ == nullptr) {
        ESP_LOGW(TAG, "Failed to initialize BMI270: %s", esp_err_to_name(ret));
        return;
    }

    ret = bsp_imu_enable_motion_detect();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to enable BMI270 any-motion: %s", esp_err_to_name(ret));
        return;
    }

    InitializeMagnetometer();
    xTaskCreate(&SensorEventInteraction::MotionTaskEntry, "halo_motion_evt", 3 * 1024, this, 4, &motion_task_handle_);
}

void SensorEventInteraction::InitializeMagnetometer() {
    esp_err_t ret = bsp_mag_init(&mag_handle_);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to initialize BMM150: %s", esp_err_to_name(ret));
        return;
    }

    mag_initialized_ = true;
}

void SensorEventInteraction::TouchTaskEntry(void* arg) {
    static_cast<SensorEventInteraction*>(arg)->TouchTaskLoop();
}

void SensorEventInteraction::MotionTaskEntry(void* arg) {
    static_cast<SensorEventInteraction*>(arg)->MotionTaskLoop();
}

void SensorEventInteraction::TouchTaskLoop() {
    while (!stop_requested_.load()) {
        EventBits_t bits = xEventGroupWaitBits(
            touch_event_group_,
            BS8112A3_TOUCH_INTERRUPT_BIT,
            pdTRUE,
            pdFALSE,
            portMAX_DELAY);

        if ((bits & BS8112A3_TOUCH_INTERRUPT_BIT) == 0) {
            continue;
        }

        uint16_t key_status = 0;
        esp_err_t ret = touch_ic_bs8112a3_get_key_status(touch_button_handle_, &key_status);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to read touch button status: %s", esp_err_to_name(ret));
            continue;
        }

        if ((key_status & kEarKeyMask) == 0) {
            continue;
        }

        if (ShouldTrigger(last_ear_trigger_us_, kEarCooldownMs)) {
            ESP_LOGI(TAG, "Ear touch detected");
            TriggerInteraction(kEarEventText);
        }

        while (!stop_requested_.load()) {
            vTaskDelay(pdMS_TO_TICKS(kTouchReleasePollMs));

            ret = touch_ic_bs8112a3_get_key_status(touch_button_handle_, &key_status);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to poll touch button release: %s", esp_err_to_name(ret));
                break;
            }

            if ((key_status & kEarKeyMask) == 0) {
                break;
            }
        }
    }

    touch_task_handle_ = nullptr;
    vTaskDelete(nullptr);
}

void SensorEventInteraction::MotionTaskLoop() {
    while (!stop_requested_.load()) {
        const int64_t now_us = esp_timer_get_time();
        uint16_t int_status = 0;
        esp_err_t ret = bsp_imu_get_event_status(&int_status);
        if (ret == ESP_OK && (int_status & BMI270_ANY_MOT_STATUS_MASK) != 0) {
            if (ShouldTrigger(last_motion_trigger_us_, kMotionCooldownMs)) {
                ESP_LOGI(TAG, "BMI270 any-motion detected");
                TriggerInteraction(kMotionEventText);
            }
        } else if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to read BMI270 event status: %s", esp_err_to_name(ret));
        }

        if (mag_initialized_ && (now_us - last_mag_poll_us_) >= static_cast<int64_t>(kMagPollMs) * 1000LL) {
            last_mag_poll_us_ = now_us;
            ProcessMagnetometerSample();
        }

        vTaskDelay(pdMS_TO_TICKS(kMotionPollMs));
    }

    motion_task_handle_ = nullptr;
    vTaskDelete(nullptr);
}

void SensorEventInteraction::ProcessMagnetometerSample() {
    struct bmm150_mag_data mag_data = {};
    int8_t mag_ret = bmm150_aux_adapter_read_mag_data(&mag_handle_, &mag_data);
    if (mag_ret != BMM150_OK) {
        ESP_LOGW(TAG, "Failed to read BMM150 data: %d", mag_ret);
        return;
    }

    const float mx = static_cast<float>(mag_data.x);
    const float my = static_cast<float>(mag_data.y);
    const float mz = static_cast<float>(mag_data.z);
    const float strength = bmm150_aux_adapter_calculate_strength(mx, my, mz);
    const bool field_normal = bmm150_aux_adapter_is_field_normal(strength);
    const int64_t now_us = esp_timer_get_time();
    if (field_normal) {
        magnetic_anomaly_since_us_ = 0;
        magnetic_anomaly_reported_ = false;
        return;
    }

    if (magnetic_anomaly_reported_) {
        return;
    }

    if (magnetic_anomaly_since_us_ == 0) {
        magnetic_anomaly_since_us_ = now_us;
        return;
    }

    if ((now_us - magnetic_anomaly_since_us_) <
        static_cast<int64_t>(kMagneticAnomalyStabilityMs) * 1000LL) {
        return;
    }

    if (!ShouldTrigger(last_magnetic_anomaly_trigger_us_, kMagneticAnomalyCooldownMs)) {
        return;
    }

    ESP_LOGI(TAG, "Magnetic anomaly detected (strength=%.1f uT, x=%.1f, y=%.1f, z=%.1f)",
             strength, mx, my, mz);
    magnetic_anomaly_reported_ = true;
    TriggerInteraction(kMagneticAnomalyEventText);
}

bool SensorEventInteraction::ShouldTrigger(int64_t& last_trigger_us, int cooldown_ms) {
    const int64_t now_us = esp_timer_get_time();
    const int64_t cooldown_us = static_cast<int64_t>(cooldown_ms) * 1000LL;

    std::lock_guard<std::mutex> lock(trigger_mutex_);
    if ((now_us - last_trigger_us) < cooldown_us) {
        return false;
    }

    last_trigger_us = now_us;
    return true;
}

void SensorEventInteraction::TriggerInteraction(std::string_view event_text) {
    Application::GetInstance().TriggerDeviceInteraction(std::string(event_text));
}
