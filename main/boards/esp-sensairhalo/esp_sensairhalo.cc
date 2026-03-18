#include "wifi_board.h"
#include "application.h"
#include "bsp_audio_codec.h"
#include "camera_http_preview.h"
#include "camera_motion_auto_talk.h"
#include "sensor_event_interaction.h"
#include "esp_video.h"

#include "bsp/display.h"
#include "bsp/esp_sensair_halo.h"
#include "display/emote_display.h"
#include "display/lcd_display.h"

#include <esp_lcd_touch.h>
#include <esp_log.h>
#include <wifi_manager.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <memory>

namespace {

constexpr char TAG[] = "ESP_SensairHalo";
constexpr uint32_t kTouchPollIntervalMs = 50;

}  // namespace

class EspSensairHalo : public WifiBoard {
private:
    esp_lcd_touch_handle_t touch_handle_ = nullptr;
    Display* display_ = nullptr;
    EspVideo* camera_ = nullptr;
    std::unique_ptr<CameraHttpPreviewServer> preview_server_;
    std::unique_ptr<CameraMotionAutoTalk> motion_auto_talk_;
    std::unique_ptr<SensorEventInteraction> sensor_event_interaction_;

    void InitializeBsp() {
        ESP_ERROR_CHECK(bsp_board_init());
        ESP_ERROR_CHECK(bsp_i2c_init());
    }

    static void TouchEventTask(void* arg) {
        auto* board = static_cast<EspSensairHalo*>(arg);
        bool was_touched = false;
        esp_lcd_touch_point_data_t touch_point[1];

        while (true) {
            uint8_t point_count = 0;
            esp_lcd_touch_read_data(board->touch_handle_);
            esp_err_t ret = esp_lcd_touch_get_data(board->touch_handle_, touch_point, &point_count, 1);
            bool is_touched = (ret == ESP_OK) && (point_count > 0);

            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Touch read failed: %s", esp_err_to_name(ret));
            } else if (!is_touched && was_touched) {
                auto& app = Application::GetInstance();
                if (app.GetDeviceState() == kDeviceStateStarting) {
                    board->EnterWifiConfigMode();
                } else {
                    app.ToggleChatState();
                }
            }

            was_touched = is_touched;
            vTaskDelay(pdMS_TO_TICKS(kTouchPollIntervalMs));
        }
    }

    void InitializeTouch() {
        ESP_ERROR_CHECK(bsp_touch_new(&touch_handle_));
        xTaskCreate(TouchEventTask, "touch_task", 3 * 1024, this, 5, nullptr);
    }

    void InitializeDisplay() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;
        bsp_display_config_t spi_cfg = {
            .max_transfer_sz = BSP_LCD_H_RES * 20 * 2,
        };
        bsp_display_parlio_config_t parlio_cfg = {
            .max_transfer_bytes = BSP_LCD_H_RES * 20 * 2,
            .dma_burst_size = 0,
            .trans_queue_depth = 0,
        };

        ESP_ERROR_CHECK(bsp_display_new(&spi_cfg, &parlio_cfg, &panel, &panel_io));

#ifdef CONFIG_USE_EMOTE_MESSAGE_STYLE
        display_ = new emote::EmoteDisplay(panel, panel_io, BSP_LCD_H_RES, BSP_LCD_V_RES);
#else
        display_ = new SpiLcdDisplay(panel_io, panel, BSP_LCD_H_RES, BSP_LCD_V_RES, 0, 0, false, true, true);
#endif
    }

    void InitializeCamera() {
        if (bsp_camera_init() != ESP_OK) {
            ESP_LOGE(TAG, "bsp_camera_init failed");
            return;
        }
        const char* dev_path = bsp_camera_get_dev_path();
        if (dev_path == nullptr || dev_path[0] == '\0') {
            ESP_LOGE(TAG, "Camera device path is invalid");
            bsp_camera_deinit();
            return;
        }
        camera_ = new EspVideo(dev_path, false);
        preview_server_ = std::make_unique<CameraHttpPreviewServer>(camera_);
        motion_auto_talk_ = std::make_unique<CameraMotionAutoTalk>(camera_);
        motion_auto_talk_->InitializeMcpTools();
        ESP_LOGI(TAG, "Camera initialized: %s", dev_path);
    }

public:
    EspSensairHalo() {
        InitializeBsp();
        sensor_event_interaction_ = std::make_unique<SensorEventInteraction>();
        InitializeTouch();
        InitializeDisplay();
        InitializeCamera();
    }

    AudioCodec* GetAudioCodec() override {
        static BspAudioCodec audio_codec;
        return &audio_codec;
    }

    void SetNetworkEventCallback(NetworkEventCallback callback) override {
        WifiBoard::SetNetworkEventCallback([this, callback = std::move(callback)](NetworkEvent event, const std::string& data) {
            if (event == NetworkEvent::Connected && preview_server_ != nullptr) {
                if (!preview_server_->IsRunning() && !preview_server_->Start()) {
                    ESP_LOGE(TAG, "Failed to start camera preview server");
                }

                auto& wifi = WifiManager::GetInstance();
                std::string ip = wifi.GetIpAddress();
                if (preview_server_->IsRunning() && !ip.empty()) {
                    ESP_LOGI(TAG, "Camera preview URL: http://%s:%u/", ip.c_str(), preview_server_->port());
                }
            }

            if (callback) {
                callback(event, data);
            }
        });
    }

    Display* GetDisplay() override {
        return display_;
    }

    Camera* GetCamera() override {
        return camera_;
    }
};

DECLARE_BOARD(EspSensairHalo);
