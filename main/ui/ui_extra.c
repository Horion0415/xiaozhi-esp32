#include "ui.h"
#include "ui_extra.h"
#include <string.h>
#include <esp_log.h>
#include "screens/ui_ScreenLight.h"
static const char* TAG = "ui_extra";
static bool g_inited = false;
typedef enum { PAGE_LIGHT=0, PAGE_AIRCON, PAGE_MUSIC, PAGE_KEYBOARD } ui_page_t;
typedef enum { MODE_HUE=0, MODE_BRIGHTNESS } light_mode_t;
static ui_page_t s_page = PAGE_LIGHT;
static bool s_light_on = true;
static int s_hue = 60;
static int s_brightness = 60;
static light_mode_t s_mode = MODE_HUE;

static lv_color_t hsv_to_color(int h, int s, int v) {
    int c = (v * s) / 100;
    int x = c * (100 - abs((h/60)%2*100 - 100)) / 100;
    int m = v - c;
    int r=0,g=0,b=0;
    if(h<60){r=c;g=x;b=0;} else if(h<120){r=x;g=c;b=0;} else if(h<180){r=0;g=c;b=x;} else if(h<240){r=0;g=x;b=c;} else if(h<300){r=x;g=0;b=c;} else {r=c;g=0;b=x;}
    r = (r + m) * 255 / 100; g = (g + m) * 255 / 100; b = (b + m) * 255 / 100;
    return lv_color_make(r,g,b);
}

static void light_apply(void) {
    if (!ui_ScreenLight) return;
    int s = s_light_on ? 100 : 0;
    int v = s_light_on ? s_brightness : 50;
    lv_color_t col = hsv_to_color(s_hue%360, s, v);
    lv_color_t gray = lv_color_hex(0x808080);
    bool on = s_light_on;
    lv_obj_set_style_arc_color(ui_ArcColorTemScreenLight, on?col:gray, LV_PART_INDICATOR|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_LabelColorTemScreenLight, on?col:gray, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_LabeColorTemRatioScreenLight, on?col:gray, LV_PART_MAIN|LV_STATE_DEFAULT);
    char buf[8];
    if (s_mode==MODE_HUE) {
        lv_arc_set_range(ui_ArcColorTemScreenLight, 0, 360);
        lv_arc_set_value(ui_ArcColorTemScreenLight, s_hue);
        lv_label_set_text(ui_LabelColorTemScreenLight, "HUE");
        snprintf(buf,sizeof(buf),"%d", s_hue);
    } else {
        lv_arc_set_range(ui_ArcColorTemScreenLight, 0, 100);
        lv_arc_set_value(ui_ArcColorTemScreenLight, s_brightness);
        lv_label_set_text(ui_LabelColorTemScreenLight, "BRI");
        snprintf(buf,sizeof(buf),"%d", s_brightness);
    }
    lv_label_set_text(ui_LabeColorTemRatioScreenLight, buf);
}
void ui_extra_init(void) {
    if (g_inited) return;
    ESP_LOGI(TAG, "ui_extra_init");
    g_inited = true;
    s_page = PAGE_LIGHT;
    light_apply();
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
        s_page = PAGE_LIGHT;
        light_apply();
    } else if (strcmp(name, "aircon") == 0) {
        lv_disp_load_scr(ui_ScreenAirCon);
        s_page = PAGE_AIRCON;
    } else if (strcmp(name, "music") == 0) {
        lv_disp_load_scr(ui_ScreenMusic);
        s_page = PAGE_MUSIC;
    } else if (strcmp(name, "keyboard") == 0) {
        lv_disp_load_scr(ui_ScreenKeyBoard);
        s_page = PAGE_KEYBOARD;
    }
}

void ui_extra_on_button(bsp_button_source_t source, bsp_button_event_t event) {
    if (!g_inited) return;
    if (event != BSP_BUTTON_EVENT_PRESS_UP) return;
    if (s_page == PAGE_LIGHT) {
        switch (source) {
            case BSP_INPUT_TOUCH_LEFT:
                if (s_mode==MODE_HUE) s_hue = (s_hue+360-10)%360; else s_brightness = s_brightness>0? s_brightness-5:0;
                ESP_LOGI(TAG, "light dec: hue=%d bri=%d", s_hue, s_brightness);
                light_apply();
                break;
            case BSP_INPUT_TOUCH_RIGHT:
                if (s_mode==MODE_HUE) s_hue = (s_hue+10)%360; else s_brightness = s_brightness<100? s_brightness+5:100;
                ESP_LOGI(TAG, "light inc: hue=%d bri=%d", s_hue, s_brightness);
                light_apply();
                break;
            case BSP_INPUT_TOUCH_TOP_LEFT:
                if (s_mode==MODE_HUE) s_hue = (s_hue+360-10)%360; else s_brightness = s_brightness>0? s_brightness-5:0;
                ESP_LOGI(TAG, "light TL: hue=%d bri=%d", s_hue, s_brightness);
                light_apply();
                break;
            case BSP_INPUT_TOUCH_TOP_RIGHT:
                if (s_mode==MODE_HUE) s_hue = (s_hue+10)%360; else s_brightness = s_brightness<100? s_brightness+5:100;
                ESP_LOGI(TAG, "light TR: hue=%d bri=%d", s_hue, s_brightness);
                light_apply();
                break;
            case BSP_INPUT_TOUCH_BOTTOM_LEFT:
                s_mode = (s_mode==MODE_HUE)? MODE_BRIGHTNESS: MODE_HUE;
                ESP_LOGI(TAG, "light toggle mode: %d", s_mode);
                light_apply();
                break;
            case BSP_INPUT_TOUCH_BOTTOM_RIGHT:
                s_light_on = !s_light_on;
                ESP_LOGI(TAG, "light onoff: %d", s_light_on);
                light_apply();
                break;
            default: break;
        }
    } else {
        if (source == BSP_INPUT_TOUCH_LEFT) {
            if (s_page==PAGE_AIRCON) ui_extra_show_screen("light");
            else if (s_page==PAGE_MUSIC) ui_extra_show_screen("aircon");
            else if (s_page==PAGE_KEYBOARD) ui_extra_show_screen("music");
        } else if (source == BSP_INPUT_TOUCH_RIGHT) {
            if (s_page==PAGE_LIGHT) ui_extra_show_screen("aircon");
            else if (s_page==PAGE_AIRCON) ui_extra_show_screen("music");
            else if (s_page==PAGE_MUSIC) ui_extra_show_screen("keyboard");
        }
    }
}


