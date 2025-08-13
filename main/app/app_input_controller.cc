#include "app_input_controller.h"
#include "bsp/esp32_c5_sensairpanel.h"

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
    if (event == BSP_BUTTON_EVENT_PRESS_DOWN) {
        bsp_led_set_for_touch(source, BSP_LED_COLOR_WHITE);
        if (self->on_press_down_) self->on_press_down_();
    } else if (event == BSP_BUTTON_EVENT_PRESS_UP) {
        bsp_led_clear_for_touch(source);
        if (self->on_press_up_) self->on_press_up_();
    }
}


