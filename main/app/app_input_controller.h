#pragma once

#include <functional>
#include "bsp/esp32_c5_sensairpanel.h"

class AppInputController {
public:
    using TouchHandler = std::function<void()>;
    AppInputController(TouchHandler on_press_down, TouchHandler on_press_up);
    static void RegisterCallbacks(AppInputController* self);
private:
    static void TouchCallback(bsp_button_source_t source, bsp_button_event_t event, void* user_data);
    TouchHandler on_press_down_;
    TouchHandler on_press_up_;
};


