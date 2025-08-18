#pragma once
#include "lvgl.h"
#ifdef __cplusplus
extern "C" {
#endif
void ui_extra_init(void);
void ui_extra_set_status(const char* text);
void ui_extra_show_screen(const char* name);
#ifdef __cplusplus
}
#endif


