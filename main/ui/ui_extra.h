#pragma once
#include "lvgl.h"
#include "bsp/esp32_c5_sensairpanel.h"
#ifdef __cplusplus
extern "C" {
#endif
void ui_extra_init(void);
void ui_extra_set_status(const char* text);
void ui_extra_show_screen(const char* name);
void ui_extra_on_button(bsp_button_source_t source, bsp_button_event_t event);
void ui_extra_on_enter_lvgl(void);
#ifdef __cplusplus
}
#endif


