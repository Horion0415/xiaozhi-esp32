#include "matter_light_controller.h"

MatterLightController::MatterLightController() {
    adapter_ = std::make_unique<CurrentLightControllerAdapter>();
    
    adapter_->RegisterStateChangeCallback([this]() {
        SyncToMatter();
        if (state_change_callback_) {
            state_change_callback_();
        }
    });
    
    adapter_->RegisterUIUpdateCallback([this]() {
        if (ui_update_callback_) {
            ui_update_callback_();
        }
    });
    
    adapter_->RegisterButtonCallback([this](ButtonPosition pos, ButtonEvent event) {
        return HandleButtonPress(pos, event);
    });
}

void MatterLightController::SetPower(bool on) {
    adapter_->SetPower(on);
    SyncToMatter();
}

void MatterLightController::SetHue(int hue) {
    adapter_->SetHue(hue);
    SyncToMatter();
}

void MatterLightController::SetBrightness(int brightness) {
    adapter_->SetBrightness(brightness);
    SyncToMatter();
}

void MatterLightController::SetMode(LightMode mode) {
    adapter_->SetMode(mode);
}

bool MatterLightController::GetPower() const {
    return adapter_->GetPower();
}

int MatterLightController::GetHue() const {
    return adapter_->GetHue();
}

int MatterLightController::GetBrightness() const {
    return adapter_->GetBrightness();
}

LightMode MatterLightController::GetMode() const {
    return adapter_->GetMode();
}

void MatterLightController::RegisterStateChangeCallback(std::function<void()> callback) {
    state_change_callback_ = callback;
}

void MatterLightController::RegisterUIUpdateCallback(std::function<void()> callback) {
    ui_update_callback_ = callback;
}

void MatterLightController::RegisterButtonCallback(LightButtonCallback callback) {
    button_callback_ = callback;
}

void MatterLightController::OnMatterStateChange(bool power, int hue, int brightness) {
    adapter_->SetPower(power);
    adapter_->SetHue(hue);
    adapter_->SetBrightness(brightness);
}

void MatterLightController::SyncToMatter() {
    bool power = GetPower();
    int hue = GetHue();
    int brightness = GetBrightness();
}

void MatterLightController::SyncFromMatter() {
}

bool MatterLightController::HandleButtonPress(ButtonPosition pos, ButtonEvent event) {
    if (button_callback_) {
        return button_callback_(pos, event);
    }
    return false;
} 