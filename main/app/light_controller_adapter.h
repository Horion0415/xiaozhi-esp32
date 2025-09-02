#pragma once
#include "light_controller_interface.h"
#include "ui/ui_extra.h"
#include <functional>

class CurrentLightControllerAdapter : public ILightController {
private:
    std::function<void()> state_change_callback_;
    std::function<void()> ui_update_callback_;
    LightButtonCallback button_callback_;
    
public:
    CurrentLightControllerAdapter();
    virtual ~CurrentLightControllerAdapter() = default;
    
    void SetPower(bool on) override;
    void SetHue(int hue) override;
    void SetBrightness(int brightness) override;
    void SetMode(LightMode mode) override;
    
    bool GetPower() const override;
    int GetHue() const override;
    int GetBrightness() const override;
    LightMode GetMode() const override;
    
    void RegisterStateChangeCallback(std::function<void()> callback) override;
    void RegisterUIUpdateCallback(std::function<void()> callback) override;
    void RegisterButtonCallback(LightButtonCallback callback) override;
    
    static void OnUIStateChange();
    static bool OnUIButtonPress(ui_button_position_t pos, ui_button_event_t event);
};

class CurrentLightUI : public ILightUI {
public:
    void UpdatePowerState(bool on) override;
    void UpdateHue(int hue) override;
    void UpdateBrightness(int brightness) override;
    void UpdateMode(LightMode mode) override;
    void UpdateIntegrationMode(IntegrationMode mode) override;
    
    void GetCurrentRGB(uint8_t* r, uint8_t* g, uint8_t* b) override;
}; 