#include "app_input_controller.h"
#include "bsp/esp32_c5_sensairpanel.h"
#include "ui/ui_extra.h"
#include <wifi_station.h>

static AppInputController* s_instance = nullptr;

AppInputController::AppInputController(TouchHandler on_press_down, TouchHandler on_press_up)
    : on_press_down_(std::move(on_press_down)), on_press_up_(std::move(on_press_up)) {}

void AppInputController::RegisterCallbacks(AppInputController* self)
{
    s_instance = self;
    bsp_button_init(TouchCallback);
}

void AppInputController::TouchCallback(bsp_button_source_t source, bsp_button_event_t event, void* user_data)
{
    auto* self = s_instance;
    if (!self) return;
    
    auto& wifi_station = WifiStation::GetInstance();
    bool wifi_connected = wifi_station.IsConnected();
    
    switch (event) {
        case BSP_BUTTON_EVENT_PRESS_DOWN: {
            bsp_led_set_for_touch(source, BSP_LED_COLOR_WHITE);
            if (wifi_connected) {
                uint8_t r=0,g=0,b=0; ui_extra_get_arc_rgb(&r,&g,&b); bsp_led_set_rgb_for_touch(source, r,g,b);
            }
            if (self->on_press_down_) self->on_press_down_();
            if (wifi_connected) {
                ui_extra_on_button(source, event);
            }
            break;
        }
        case BSP_BUTTON_EVENT_LONG_PRESS: {
            if (wifi_connected) {
                ui_extra_on_button(source, event);
            }
            break;
        }
        case BSP_BUTTON_EVENT_PRESS_UP: {
            bsp_led_clear_for_touch(source);
            if (wifi_connected) {
                ui_extra_on_button(source, event);
            }
            if (self->on_press_up_) self->on_press_up_();
            break;
        }
        default: {
            if (wifi_connected) {
                ui_extra_on_button(source, event);
            }
            break;
        }
    }
}


