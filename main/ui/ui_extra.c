#include "ui.h"
#include "ui_extra.h"
#include <string.h>
#include <esp_log.h>
#include "screens/ui_ScreenLight.h"
#include "bsp/display.h"
static const char* TAG = "ui_extra";
static bool g_inited = false;
typedef enum { PAGE_LIGHT=0, PAGE_AIRCON, PAGE_MUSIC, PAGE_KEYBOARD } ui_page_t;
typedef enum { MODE_HUE=0, MODE_BRIGHTNESS } light_mode_t;
typedef enum { INTEG_MATTER=0, INTEG_HA, INTEG_RAINMAKER } integ_mode_t;
static ui_page_t s_page = PAGE_LIGHT;
static bool s_light_on = true;
static int s_hue = 60;
static int s_brightness = 60;
static light_mode_t s_mode = MODE_HUE;
static integ_mode_t s_integration = INTEG_MATTER;
static bool s_bl_longpress = false;

static lv_color_t hsv_to_color(int h, int s, int v) {
    int c = (v * s) / 100;
    int x = c * (100 - abs((h/60)%2*100 - 100)) / 100;
    int m = v - c;
    int r=0,g=0,b=0;
    if(h<60){r=c;g=x;b=0;} else if(h<120){r=x;g=c;b=0;} else if(h<180){r=0;g=c;b=x;} else if(h<240){r=0;g=x;b=c;} else if(h<300){r=x;g=0;b=c;} else {r=c;g=0;b=x;}
    r = (r + m) * 255 / 100; g = (g + m) * 255 / 100; b = (b + m) * 255 / 100;
    return lv_color_make(r,g,b);
}
void ui_extra_get_arc_rgb(uint8_t* r, uint8_t* g, uint8_t* b) {
    int H = (s_hue % 360);
    int S = s_light_on ? 100 : 0;
    int V = s_light_on ? s_brightness : 50;
    int C = (V * S) / 100;
    int X = C * (100 - abs((H/60)%2*100 - 100)) / 100;
    int m = V - C;
    int rr=0,gg=0,bb=0;
    if(H<60){rr=C;gg=X;bb=0;} else if(H<120){rr=X;gg=C;bb=0;} else if(H<180){rr=0;gg=C;bb=X;} else if(H<240){rr=0;gg=X;bb=C;} else if(H<300){rr=X;gg=0;bb=C;} else {rr=C;gg=0;bb=X;}
    rr = (rr + m) * 255 / 100;
    gg = (gg + m) * 255 / 100;
    bb = (bb + m) * 255 / 100;
    *r = (uint8_t)rr;
    *g = (uint8_t)gg;
    *b = (uint8_t)bb;
}

static void light_apply(void) {
    if (!ui_ScreenLight) return;
    int s = s_light_on ? 100 : 0;
    int v = s_light_on ? s_brightness : 50;
    lv_color_t col = hsv_to_color(s_hue%360, s, v);
    lv_color_t gray = lv_color_hex(0x808080);
    lv_color_t txt_on = lv_color_hex(0xFFFFFF);
    bool on = s_light_on;
    lv_obj_set_style_arc_color(ui_ArcColorTemScreenLight, on?col:gray, LV_PART_INDICATOR|LV_STATE_DEFAULT);
    char buf[8];
    if (s_mode==MODE_HUE) {
        lv_arc_set_range(ui_ArcColorTemScreenLight, 0, 360);
        lv_arc_set_value(ui_ArcColorTemScreenLight, s_hue);
        lv_label_set_text(ui_LabelColorTemScreenLight, "CCT");
        lv_label_set_text(ui_LabelColorTemDecScreenLight, "CCT-");
        lv_label_set_text(ui_LabelColorTemAddScreenLight, "CCT+");
        snprintf(buf,sizeof(buf),"%d", s_hue);
    } else {
        lv_arc_set_range(ui_ArcColorTemScreenLight, 0, 100);
        lv_arc_set_value(ui_ArcColorTemScreenLight, s_brightness);
        lv_label_set_text(ui_LabelColorTemScreenLight, "DIM");
        lv_label_set_text(ui_LabelColorTemDecScreenLight, "DIM-");
        lv_label_set_text(ui_LabelColorTemAddScreenLight, "DIM+");
        snprintf(buf,sizeof(buf),"%d", s_brightness);
    }
    lv_label_set_text(ui_LabeColorTemRatioScreenLight, buf);
    lv_obj_set_style_text_color(ui_LabelColorTemScreenLight, on?txt_on:gray, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_LabeColorTemRatioScreenLight, on?txt_on:gray, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_LabelColorTemDecScreenLight, on?txt_on:gray, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_LabelColorTemAddScreenLight, on?txt_on:gray, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_LabelColorTemCheckScreenLight, on?txt_on:gray, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_LabelColorTemOnOffScreenLight, on?txt_on:gray, LV_PART_MAIN|LV_STATE_DEFAULT);
    switch (s_integration) {
        case INTEG_MATTER: lv_label_set_text(ui_LabelColorTemModeScreenLight, "Matter"); break;
        case INTEG_HA: lv_label_set_text(ui_LabelColorTemModeScreenLight, "HA"); break;
        case INTEG_RAINMAKER: lv_label_set_text(ui_LabelColorTemModeScreenLight, "Rainmaker"); break;
    }
    lv_obj_set_style_text_color(ui_LabelColorTemModeScreenLight, on?txt_on:gray, LV_PART_MAIN|LV_STATE_DEFAULT);
}

static void light_fullscreen(void) {
    if (!ui_ImageColorTemScreenLight) return;
    lv_display_t* d = lv_display_get_default();
    if (!d) return;
    int w = lv_display_get_horizontal_resolution(d);
    int h = lv_display_get_vertical_resolution(d);
    ESP_LOGI(TAG, "set light bg to full screen %dx%d", w, h);
    lv_obj_set_size(ui_ImageColorTemScreenLight, w, h);
    lv_obj_set_align(ui_ImageColorTemScreenLight, LV_ALIGN_TOP_LEFT);
}
void ui_extra_init(void) {
    if (g_inited) return;
    ESP_LOGI(TAG, "ui_extra_init");
    g_inited = true;
    s_page = PAGE_LIGHT;
    light_fullscreen();
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
    if (bsp_display_lock(0)) {
        if (strcmp(name, "light") == 0) {
            lv_disp_load_scr(ui_ScreenLight);
            s_page = PAGE_LIGHT;
            light_fullscreen();
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
        bsp_display_unlock();
    }
}

void ui_extra_on_button(bsp_button_source_t source, bsp_button_event_t event) {
    if (!g_inited) return;
    if (bsp_display_lock(0)) {
        if (event == BSP_BUTTON_EVENT_LONG_PRESS) {
            if (s_page == PAGE_LIGHT && source == BSP_INPUT_TOUCH_BOTTOM_LEFT) {
                s_bl_longpress = true;
                if (s_integration == INTEG_MATTER) s_integration = INTEG_HA; else if (s_integration == INTEG_HA) s_integration = INTEG_RAINMAKER; else s_integration = INTEG_MATTER;
                ESP_LOGI(TAG, "integration mode: %d", s_integration);
                light_apply();
            }
            bsp_display_unlock();
            return;
        }
        if (event != BSP_BUTTON_EVENT_PRESS_UP) { bsp_display_unlock(); return; }
        if (source == BSP_INPUT_TOUCH_LEFT) {
            if (s_page==PAGE_LIGHT) ui_extra_show_screen("keyboard");
            else if (s_page==PAGE_AIRCON) ui_extra_show_screen("light");
            else if (s_page==PAGE_MUSIC) ui_extra_show_screen("aircon");
            else if (s_page==PAGE_KEYBOARD) ui_extra_show_screen("music");
            bsp_display_unlock();
            return;
        } else if (source == BSP_INPUT_TOUCH_RIGHT) {
            if (s_page==PAGE_LIGHT) ui_extra_show_screen("aircon");
            else if (s_page==PAGE_AIRCON) ui_extra_show_screen("music");
            else if (s_page==PAGE_MUSIC) ui_extra_show_screen("keyboard");
            else if (s_page==PAGE_KEYBOARD) ui_extra_show_screen("light");
            bsp_display_unlock();
            return;
        }
        if (s_page == PAGE_LIGHT) {
        switch (source) {
            case BSP_INPUT_TOUCH_TOP_LEFT:
                if (!s_light_on) { ESP_LOGI(TAG, "light is off, ignore value/mode changes"); break; }
                if (s_mode==MODE_HUE) s_hue = (s_hue+360-10)%360; else s_brightness = s_brightness>0? s_brightness-5:0;
                ESP_LOGI(TAG, "light TL: hue=%d bri=%d", s_hue, s_brightness);
                light_apply();
                break;
            case BSP_INPUT_TOUCH_TOP_RIGHT:
                if (!s_light_on) { ESP_LOGI(TAG, "light is off, ignore value/mode changes"); break; }
                if (s_mode==MODE_HUE) s_hue = (s_hue+10)%360; else s_brightness = s_brightness<100? s_brightness+5:100;
                ESP_LOGI(TAG, "light TR: hue=%d bri=%d", s_hue, s_brightness);
                light_apply();
                break;
            case BSP_INPUT_TOUCH_BOTTOM_LEFT:
                if (s_bl_longpress) { s_bl_longpress = false; ESP_LOGI(TAG, "skip mode toggle after long press"); break; }
                if (!s_light_on) { ESP_LOGI(TAG, "light is off, ignore value/mode changes"); break; }
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
        }
        bsp_display_unlock();
    }
}

void ui_extra_on_enter_lvgl(void) {
    if (!g_inited) return;
    if (bsp_display_lock(0)) {
        lv_disp_t* d = lv_disp_get_default();
        if (d) {
            lv_obj_t* scr = lv_disp_get_scr_act(d);
            if (scr) lv_obj_invalidate(scr);
        }
        if (s_page == PAGE_LIGHT) {
            light_fullscreen();
            light_apply();
        } else if (s_page == PAGE_AIRCON) {
            ui_extra_show_screen("aircon");
        } else if (s_page == PAGE_MUSIC) {
            ui_extra_show_screen("music");
        } else if (s_page == PAGE_KEYBOARD) {
            ui_extra_show_screen("keyboard");
        }
        bsp_display_unlock();
    }
}


