#include "ui.h"
#include "ui_extra.h"
#include <string.h>
#include <esp_log.h>
static const char* TAG = "ui_extra";
static bool g_inited = false;
void ui_extra_init(void) {
    if (g_inited) return;
    ESP_LOGI(TAG, "ui_extra_init");
    g_inited = true;
}
void ui_extra_set_status(const char* text) {
    if (!g_inited) return;
    ESP_LOGI(TAG, "ui_extra_set_status: %s", text ? text : "");
}
void ui_extra_show_screen(const char* name) {
    if (!g_inited) return;
    if (!name) return;
    ESP_LOGI(TAG, "ui_extra_show_screen: %s", name);
    if (strcmp(name, "light") == 0) {
        lv_disp_load_scr(ui_ScreenLight);
    } else if (strcmp(name, "aircon") == 0) {
        lv_disp_load_scr(ui_ScreenAirCon);
    } else if (strcmp(name, "music") == 0) {
        lv_disp_load_scr(ui_ScreenMusic);
    } else if (strcmp(name, "keyboard") == 0) {
        lv_disp_load_scr(ui_ScreenKeyBoard);
    }
}


