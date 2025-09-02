#pragma once
#include "light_controller_interface.h"
#include "light_controller_adapter.h"
#include <memory>

class LightIntegrationManager {
private:
    std::unique_ptr<ILightController> current_controller_;
    std::unique_ptr<ILightUI> ui_;
    IntegrationMode current_mode_;
    
    static LightIntegrationManager* instance_;
    
public:
    static LightIntegrationManager& GetInstance();
    
    LightIntegrationManager();
    ~LightIntegrationManager() = default;
    
    void Initialize();
    void SwitchToMatter();
    void SwitchToHA();
    void SwitchToRainmaker();
    void HandleIntegrationSwitch();
    
    ILightController* GetCurrentController() { return current_controller_.get(); }
    ILightUI* GetUI() { return ui_.get(); }
    IntegrationMode GetCurrentMode() const { return current_mode_; }
    
    static std::unique_ptr<ILightController> CreateController(IntegrationMode mode);
}; 