#include "light_integration_manager.h"
#include "light_controllers/matter_light_controller.h"
#include "light_controllers/ha_light_controller.h"
#include "light_controllers/rainmaker_light_controller.h"

LightIntegrationManager* LightIntegrationManager::instance_ = nullptr;

LightIntegrationManager& LightIntegrationManager::GetInstance() {
    if (!instance_) {
        instance_ = new LightIntegrationManager();
    }
    return *instance_;
}

LightIntegrationManager::LightIntegrationManager() 
    : current_mode_(IntegrationMode::MATTER) {
}

void LightIntegrationManager::Initialize() {
    ui_ = std::make_unique<CurrentLightUI>();
    current_controller_ = CreateController(current_mode_);
}

void LightIntegrationManager::SwitchToMatter() {
    current_controller_ = CreateController(IntegrationMode::MATTER);
    current_mode_ = IntegrationMode::MATTER;
    ui_->UpdateIntegrationMode(IntegrationMode::MATTER);
}

void LightIntegrationManager::SwitchToHA() {
    current_controller_ = CreateController(IntegrationMode::HA);
    current_mode_ = IntegrationMode::HA;
    ui_->UpdateIntegrationMode(IntegrationMode::HA);
}

void LightIntegrationManager::SwitchToRainmaker() {
    current_controller_ = CreateController(IntegrationMode::RAINMAKER);
    current_mode_ = IntegrationMode::RAINMAKER;
    ui_->UpdateIntegrationMode(IntegrationMode::RAINMAKER);
}

void LightIntegrationManager::HandleIntegrationSwitch() {
    switch(current_mode_) {
        case IntegrationMode::MATTER: 
            SwitchToHA(); 
            break;
        case IntegrationMode::HA: 
            SwitchToRainmaker(); 
            break;
        case IntegrationMode::RAINMAKER: 
            SwitchToMatter(); 
            break;
    }
}

std::unique_ptr<ILightController> LightIntegrationManager::CreateController(IntegrationMode mode) {
    switch(mode) {
        case IntegrationMode::MATTER:
            return std::make_unique<MatterLightController>();
        case IntegrationMode::HA:
            return std::make_unique<HALightController>();
        case IntegrationMode::RAINMAKER:
            return std::make_unique<RainmakerLightController>();
        default:
            return std::make_unique<CurrentLightControllerAdapter>();
    }
} 