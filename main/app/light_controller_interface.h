#pragma once
#include <functional>

enum class LightMode {
    HUE = 0,
    BRIGHTNESS = 1
};

enum class IntegrationMode {
    MATTER = 0,
    HA = 1, 
    RAINMAKER = 2
};

enum class ButtonPosition {
    TOP_LEFT,
    TOP_RIGHT,
    BOTTOM_LEFT,
    BOTTOM_RIGHT
};

enum class ButtonEvent {
    PRESS_DOWN,
    PRESS_UP,
    LONG_PRESS,
    SHORT_PRESS
};

typedef bool (*LightButtonCallback)(ButtonPosition pos, ButtonEvent event);

class ILightController {
public:
    virtual ~ILightController() = default;
    
    virtual void SetPower(bool on) = 0;
    virtual void SetHue(int hue) = 0;
    virtual void SetBrightness(int brightness) = 0;
    virtual void SetMode(LightMode mode) = 0;
    
    virtual bool GetPower() const = 0;
    virtual int GetHue() const = 0;
    virtual int GetBrightness() const = 0;
    virtual LightMode GetMode() const = 0;
    
    virtual void RegisterStateChangeCallback(std::function<void()> callback) = 0;
    virtual void RegisterUIUpdateCallback(std::function<void()> callback) = 0;
    virtual void RegisterButtonCallback(LightButtonCallback callback) = 0;
};

class ILightEventHandler {
public:
    virtual ~ILightEventHandler() = default;
    virtual bool OnButtonPress(ButtonPosition position, ButtonEvent event) = 0;
};

class ILightUI {
public:
    virtual ~ILightUI() = default;
    
    virtual void UpdatePowerState(bool on) = 0;
    virtual void UpdateHue(int hue) = 0;
    virtual void UpdateBrightness(int brightness) = 0;
    virtual void UpdateMode(LightMode mode) = 0;
    virtual void UpdateIntegrationMode(IntegrationMode mode) = 0;
    
    virtual void GetCurrentRGB(uint8_t* r, uint8_t* g, uint8_t* b) = 0;
}; 