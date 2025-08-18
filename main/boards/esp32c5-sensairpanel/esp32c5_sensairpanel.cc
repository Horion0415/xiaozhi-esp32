#include "wifi_board.h"
#include "boards/esp32c5-sensairpanel/config.h"
#include "boards/esp32c5-sensairpanel/emote_display.h"
#include "boards/esp-hi/adc_pdm_audio_codec.h"
#include "display/lcd_display.h"
#include "application.h"
#include "button.h"
#include <wifi_station.h>

#include <esp_log.h>
#include <esp_lcd_panel_io.h>
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "esp_lvgl_port.h"
#include "esp_lvgl_port_disp.h"
#include "bsp/esp32_c5_sensairpanel.h"
#include "lvgl.h"
#include "app/display_mode_controller.h"
#include "app/app_input_controller.h"
#include "device_state_event.h"
#include "ui/ui.h"
#include "ui/ui_extra.h"

LV_FONT_DECLARE(font_puhui_16_4);
LV_FONT_DECLARE(font_awesome_16_4);

static const char *TAG = "C5_Sensairpanel";

static DisplayModeController* g_display_mode = nullptr;
static AppInputController* g_input_ctrl = nullptr;

static void on_touch_down()
{
    if (g_display_mode) g_display_mode->EnterLvglMode();
    auto disp = Board::GetInstance().GetDisplay();
    if (disp) {
        static_cast<anim::EmoteDisplay*>(disp)->EnterLvglMode();
    }
    Application::GetInstance().Schedule([](){
        auto &app = Application::GetInstance();
        auto state = app.GetDeviceState();
        if (state == kDeviceStateSpeaking) {
            app.AbortSpeaking(kAbortReasonNone);
        } else if (state == kDeviceStateListening) {
            app.StopListening();
        }
    });
}

static void on_touch_up()
{
}

class C5Sensairpanel : public WifiBoard {
private:
    Button boot_button_ { GPIO_NUM_28 };
    Display *display_ = nullptr;
    std::unique_ptr<AudioCodec> audio_codec_;

    void InitPowerRail() { bsp_power_control_set_power(true); }

    void InitDisplay() {
        bsp_display_cfg_t cfg = {
            .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
            .buffer_size = DISPLAY_WIDTH * DISPLAY_HEIGHT,
            .double_buffer = 1,
            .flags = {
                .buff_dma = 0,
                .buff_spiram = 1,
            },
        };
        auto lv_disp = bsp_display_start_with_config(&cfg);
        ESP_LOGI(TAG, "LVGL display started: %p", lv_disp);
        display_ = new anim::EmoteDisplay(lv_disp);
        g_display_mode = new DisplayModeController(lv_disp);
        g_input_ctrl = new AppInputController(on_touch_down, on_touch_up);
        AppInputController::RegisterCallbacks(g_input_ctrl);
        static bool ui_inited = false;
        if (!ui_inited) {
            ui_init();
            ui_extra_init();
            ui_inited = true;
        }
    }

    void InitBacklight() {
        ESP_ERROR_CHECK(bsp_display_brightness_init());
        ESP_ERROR_CHECK(bsp_display_backlight_on());
    }

    void InitButtons() {
        boot_button_.OnClick([this]() {
            auto &app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
            }
            app.ToggleChatState();
        });
    }

public:
    C5Sensairpanel() {
        ESP_LOGI(TAG, "Init board");
        InitPowerRail();
        InitDisplay();
        InitBacklight();
        if (display_) {
            static_cast<anim::EmoteDisplay*>(display_)->EnterGfxMode();
        }
        if (g_display_mode) {
            g_display_mode->EnterGfxMode();
        }
        ESP_ERROR_CHECK(bsp_led_init());
        InitButtons();
        DeviceStateEventManager::GetInstance().RegisterStateChangeCallback([](DeviceState prev, DeviceState cur){
            if (cur == kDeviceStateListening) {
                if (g_display_mode) g_display_mode->EnterGfxMode();
                auto disp = Board::GetInstance().GetDisplay();
                if (disp) static_cast<anim::EmoteDisplay*>(disp)->EnterGfxMode();
            }
        });
    }

    virtual std::string GetBoardType() override { return "esp32c5-sensairpanel"; }

    virtual Display* GetDisplay() override { return display_; }

    virtual Led* GetLed() override { static NoLed no; return &no; }

    virtual AudioCodec* GetAudioCodec() override {
        if (!audio_codec_) {
            audio_codec_ = std::unique_ptr<AudioCodec>(new AdcPdmAudioCodec(
                AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
                AUDIO_ADC_CHANNEL, AUDIO_PDM_SPEAK_P, AUDIO_PDM_SPEAK_N, AUDIO_PA_CTRL));
        }
        return audio_codec_.get();
    }

    virtual void SetPowerSaveMode(bool enabled) override { ESP_LOGI(TAG, "Ignoring power save request: %s", enabled ? "on" : "off"); }

    virtual std::string GetBoardJson() override {
        return std::string("{\"name\":\"ESP32-C5 Sensairpanel\"}");
    }

    virtual std::string GetDeviceStatusJson() override {
        return std::string("{\"device\":\"ok\"}");
    }
};

DECLARE_BOARD(C5Sensairpanel) 