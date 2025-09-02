#pragma once
#include "../light_controller_interface.h"
#include "../light_controller_adapter.h"
#include <functional>

class HALightController : public ILightController {
private:
    std::unique_ptr<CurrentLightControllerAdapter> adapter_;
    std::function<void()> state_change_callback_;
    std::function<void()> ui_update_callback_;
    LightButtonCallback button_callback_;
    
public:
    HALightController();
    virtual ~HALightController() = default;
    
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
    
    void OnHAStateChange(bool power, int hue, int brightness);
    
private:
    void SyncToHA();
    void SyncFromHA();
    bool HandleButtonPress(ButtonPosition pos, ButtonEvent event);
}; 