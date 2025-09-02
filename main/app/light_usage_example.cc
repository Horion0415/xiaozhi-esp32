#include "light_integration_manager.h"

void light_example_init() {
    auto& manager = LightIntegrationManager::GetInstance();
    manager.Initialize();
}

void light_example_switch_integration() {
    auto& manager = LightIntegrationManager::GetInstance();
    manager.HandleIntegrationSwitch();
}

void light_example_control() {
    auto& manager = LightIntegrationManager::GetInstance();
    auto* controller = manager.GetCurrentController();
    
    if (controller) {
        controller->SetPower(true);
        controller->SetHue(180);
        controller->SetBrightness(80);
        
        bool power = controller->GetPower();
        int hue = controller->GetHue();
        int brightness = controller->GetBrightness();
    }
}

void light_example_register_callbacks() {
    auto& manager = LightIntegrationManager::GetInstance();
    auto* controller = manager.GetCurrentController();
    
    if (controller) {
        controller->RegisterStateChangeCallback([]() {
        });
        
        controller->RegisterUIUpdateCallback([]() {
        });
        
        controller->RegisterButtonCallback([](ButtonPosition pos, ButtonEvent event) -> bool {
            switch(pos) {
                case ButtonPosition::TOP_LEFT:
                    break;
                case ButtonPosition::TOP_RIGHT:
                    break;
                case ButtonPosition::BOTTOM_LEFT:
                    break;
                case ButtonPosition::BOTTOM_RIGHT:
                    break;
            }
            return false;
        });
    }
}

bool light_example_c_style_callback(ui_button_position_t pos, ui_button_event_t event) {
    switch(pos) {
        case UI_BUTTON_POS_TOP_LEFT:
            break;
        case UI_BUTTON_POS_TOP_RIGHT:
            break;
        case UI_BUTTON_POS_BOTTOM_LEFT:
            break;
        case UI_BUTTON_POS_BOTTOM_RIGHT:
            break;
    }
    return false;
}

void light_example_c_style_register() {
    ui_extra_register_light_button_callback(light_example_c_style_callback);
} 