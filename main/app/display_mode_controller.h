#pragma once

#include <lvgl.h>
#include "esp_lvgl_port_disp.h"
#include "bsp/display.h"

class DisplayModeController {
public:
    explicit DisplayModeController(lv_display_t* disp);
    void EnterGfxMode();
    void EnterLvglMode();
private:
    lv_display_t* disp_ = nullptr;
    lv_obj_t* overlay_box_ = nullptr;
    lv_obj_t* label_ = nullptr;
    int lvgl_switch_count_ = 0;
};


