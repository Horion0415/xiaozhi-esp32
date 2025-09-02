#include "light_controller_adapter.h"

static CurrentLightControllerAdapter* g_adapter_instance = nullptr;

ButtonPosition ui_pos_to_button_pos(ui_button_position_t pos) {
    switch(pos) {
        case UI_BUTTON_POS_TOP_LEFT: return ButtonPosition::TOP_LEFT;
        case UI_BUTTON_POS_TOP_RIGHT: return ButtonPosition::TOP_RIGHT;
        case UI_BUTTON_POS_BOTTOM_LEFT: return ButtonPosition::BOTTOM_LEFT;
        case UI_BUTTON_POS_BOTTOM_RIGHT: return ButtonPosition::BOTTOM_RIGHT;
        default: return ButtonPosition::TOP_LEFT;
    }
}

ButtonEvent ui_event_to_button_event(ui_button_event_t event) {
    switch(event) {
        case UI_BUTTON_EVENT_PRESS_DOWN: return ButtonEvent::PRESS_DOWN;
        case UI_BUTTON_EVENT_PRESS_UP: return ButtonEvent::PRESS_UP;
        case UI_BUTTON_EVENT_LONG_PRESS: return ButtonEvent::LONG_PRESS;
        case UI_BUTTON_EVENT_SHORT_PRESS: return ButtonEvent::SHORT_PRESS;
        default: return ButtonEvent::SHORT_PRESS;
    }
}

CurrentLightControllerAdapter::CurrentLightControllerAdapter() {
    g_adapter_instance = this;
    ui_extra_register_light_callback(OnUIStateChange);
    ui_extra_register_light_button_callback(OnUIButtonPress);
}

void CurrentLightControllerAdapter::SetPower(bool on) {
    ui_extra_set_power_direct(on);
}

void CurrentLightControllerAdapter::SetHue(int hue) {
    ui_extra_set_hue_direct(hue);
}

void CurrentLightControllerAdapter::SetBrightness(int brightness) {
    ui_extra_set_brightness_direct(brightness);
}

void CurrentLightControllerAdapter::SetMode(LightMode mode) {
    ui_extra_set_light_mode_direct(static_cast<int>(mode));
}

bool CurrentLightControllerAdapter::GetPower() const {
    return ui_extra_get_light_power();
}

int CurrentLightControllerAdapter::GetHue() const {
    return ui_extra_get_light_hue();
}

int CurrentLightControllerAdapter::GetBrightness() const {
    return ui_extra_get_light_brightness();
}

LightMode CurrentLightControllerAdapter::GetMode() const {
    return static_cast<LightMode>(ui_extra_get_light_mode());
}

void CurrentLightControllerAdapter::RegisterStateChangeCallback(std::function<void()> callback) {
    state_change_callback_ = callback;
}

void CurrentLightControllerAdapter::RegisterUIUpdateCallback(std::function<void()> callback) {
    ui_update_callback_ = callback;
}

void CurrentLightControllerAdapter::RegisterButtonCallback(LightButtonCallback callback) {
    button_callback_ = callback;
}

void CurrentLightControllerAdapter::OnUIStateChange() {
    if (g_adapter_instance) {
        if (g_adapter_instance->state_change_callback_) {
            g_adapter_instance->state_change_callback_();
        }
        if (g_adapter_instance->ui_update_callback_) {
            g_adapter_instance->ui_update_callback_();
        }
    }
}

bool CurrentLightControllerAdapter::OnUIButtonPress(ui_button_position_t pos, ui_button_event_t event) {
    if (g_adapter_instance && g_adapter_instance->button_callback_) {
        ButtonPosition button_pos = ui_pos_to_button_pos(pos);
        ButtonEvent button_event = ui_event_to_button_event(event);
        return g_adapter_instance->button_callback_(button_pos, button_event);
    }
    return false;
}

void CurrentLightUI::UpdatePowerState(bool on) {
    ui_extra_set_power_direct(on);
}

void CurrentLightUI::UpdateHue(int hue) {
    ui_extra_set_hue_direct(hue);
}

void CurrentLightUI::UpdateBrightness(int brightness) {
    ui_extra_set_brightness_direct(brightness);
}

void CurrentLightUI::UpdateMode(LightMode mode) {
    ui_extra_set_light_mode_direct(static_cast<int>(mode));
}

void CurrentLightUI::UpdateIntegrationMode(IntegrationMode mode) {
    ui_extra_set_integration_mode_direct(static_cast<int>(mode));
}

void CurrentLightUI::GetCurrentRGB(uint8_t* r, uint8_t* g, uint8_t* b) {
    ui_extra_get_arc_rgb(r, g, b);
} 