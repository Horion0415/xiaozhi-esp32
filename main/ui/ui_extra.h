#pragma once
#include "lvgl.h"
#include "bsp/esp32_c5_sensairpanel.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_BUTTON_POS_TOP_LEFT = 0,
    UI_BUTTON_POS_TOP_RIGHT,
    UI_BUTTON_POS_BOTTOM_LEFT,
    UI_BUTTON_POS_BOTTOM_RIGHT
} ui_button_position_t;

typedef enum {
    UI_BUTTON_EVENT_PRESS_DOWN = 0,
    UI_BUTTON_EVENT_PRESS_UP,
    UI_BUTTON_EVENT_LONG_PRESS,
    UI_BUTTON_EVENT_SHORT_PRESS
} ui_button_event_t;

typedef bool (*ui_light_button_callback_t)(ui_button_position_t pos, ui_button_event_t event);

void ui_extra_init(void);
void ui_extra_set_status(const char* text);
void ui_extra_show_screen(const char* name);
void ui_extra_on_button(bsp_button_source_t source, bsp_button_event_t event);
void ui_extra_on_enter_lvgl(void);
void ui_extra_get_arc_rgb(uint8_t* r, uint8_t* g, uint8_t* b);

bool ui_extra_get_light_power(void);
int ui_extra_get_light_hue(void);
int ui_extra_get_light_brightness(void);
int ui_extra_get_light_mode(void);
int ui_extra_get_integration_mode(void);

void ui_extra_set_power_direct(bool on);
void ui_extra_set_hue_direct(int hue);
void ui_extra_set_brightness_direct(int brightness);
void ui_extra_set_light_mode_direct(int mode);
void ui_extra_set_integration_mode_direct(int integration);

void ui_extra_register_light_callback(void (*callback)(void));
void ui_extra_register_light_button_callback(ui_light_button_callback_t callback);

#ifdef __cplusplus
}
#endif


