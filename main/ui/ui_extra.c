#include "ui.h"
#include "ui_extra.h"
#include <string.h>
#include <esp_log.h>
#include "screens/ui_ScreenLight.h"
#include "screens/ui_ScreenKeyBoard.h"
#include "bsp/display.h"
#include "bsp/esp32_c5_sensairpanel.h"
#include <esp_timer.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>
#include "app/ir_ac_bridge.h"
#include "rhythm_visualizer.h"
static const char* TAG = "ui_extra";
static bool g_inited = false;
typedef enum { PAGE_LIGHT=0, PAGE_AIRCON, PAGE_MUSIC, PAGE_KEYBOARD } ui_page_t;
typedef enum { MODE_HUE=0, MODE_BRIGHTNESS } light_mode_t;
typedef enum { INTEG_MATTER=0, INTEG_HA, INTEG_RAINMAKER } integ_mode_t;
typedef enum { AIR_COOL=0, AIR_HEAT } air_mode_t;
typedef enum { BRAND_MIDEA=0, BRAND_GREE, BRAND_HAIER } air_brand_t;
static struct {
    ui_page_t page;
    bool light_on;
    int hue;
    int brightness;
    light_mode_t mode;
    integ_mode_t integration;
    bool bl_longpress;
    bool air_on;
    air_mode_t air_mode;
    int air_temp;
    bool ac_bl_longpress;
    air_brand_t air_brand;
    int music_index;
    bool music_playing;
    bool music_rhythm_initialized;
} s_state = { PAGE_LIGHT, true, 60, 60, MODE_HUE, INTEG_MATTER, false, true, AIR_COOL, 26, false, BRAND_MIDEA, 0, true, false };
static esp_timer_handle_t s_kb_audio_timer = NULL;
static void kb_audio_timer_cb(void* arg) { (void)arg; bsp_wav_stop(); }
static const int HUE_STEP = 10;
static const int BRI_STEP = 5;
static const int BRI_MIN = 0;
static const int BRI_MAX = 100;
static const int AIR_TEMP_MIN = 16;
static const int AIR_TEMP_MAX = 30;
static const int AIR_TEMP_STEP = 1;
 

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
    if (s_state.page == PAGE_AIRCON) {
        uint32_t c = s_state.air_on ? (s_state.air_mode==AIR_COOL ? 0x00BFFF : 0xFF4D4D) : 0x808080;
        *r = (uint8_t)((c >> 16) & 0xFF);
        *g = (uint8_t)((c >> 8) & 0xFF);
        *b = (uint8_t)(c & 0xFF);
        return;
    }
    int H = (s_state.hue % 360);
    int S = s_state.light_on ? 100 : 0;
    int V = s_state.light_on ? s_state.brightness : 50;
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

static void light_apply_nolock(void) {
    if (!ui_ScreenLight) return;
    int s = s_state.light_on ? 100 : 0;
    int v = s_state.light_on ? s_state.brightness : 50;
    lv_color_t col = hsv_to_color(s_state.hue%360, s, v);
    lv_color_t gray = lv_color_hex(0x808080);
    lv_color_t txt_on = lv_color_hex(0xFFFFFF);
    bool on = s_state.light_on;
    lv_obj_set_style_arc_color(ui_ArcColorTemScreenLight, on?col:gray, LV_PART_INDICATOR|LV_STATE_DEFAULT);
    char buf[8];
    if (s_state.mode==MODE_HUE) {
        lv_arc_set_range(ui_ArcColorTemScreenLight, 0, 360);
        lv_arc_set_value(ui_ArcColorTemScreenLight, s_state.hue);
        lv_label_set_text(ui_LabelColorTemScreenLight, "CCT");
        lv_label_set_text(ui_LabelColorTemDecScreenLight, "CCT-");
        lv_label_set_text(ui_LabelColorTemAddScreenLight, "CCT+");
        snprintf(buf,sizeof(buf),"%d", s_state.hue);
    } else {
        lv_arc_set_range(ui_ArcColorTemScreenLight, 0, 100);
        lv_arc_set_value(ui_ArcColorTemScreenLight, s_state.brightness);
        lv_label_set_text(ui_LabelColorTemScreenLight, "DIM");
        lv_label_set_text(ui_LabelColorTemDecScreenLight, "DIM-");
        lv_label_set_text(ui_LabelColorTemAddScreenLight, "DIM+");
        snprintf(buf,sizeof(buf),"%d", s_state.brightness);
    }
    lv_label_set_text(ui_LabeColorTemRatioScreenLight, buf);
    lv_obj_set_style_text_color(ui_LabelColorTemScreenLight, on?txt_on:gray, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_LabeColorTemRatioScreenLight, on?txt_on:gray, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_LabelColorTemDecScreenLight, on?txt_on:gray, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_LabelColorTemAddScreenLight, on?txt_on:gray, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_LabelColorTemCheckScreenLight, on?txt_on:gray, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_LabelColorTemOnOffScreenLight, on?txt_on:gray, LV_PART_MAIN|LV_STATE_DEFAULT);
    switch (s_state.integration) {
        case INTEG_MATTER: lv_label_set_text(ui_LabelColorTemModeScreenLight, "Matter"); break;
        case INTEG_HA: lv_label_set_text(ui_LabelColorTemModeScreenLight, "HA"); break;
        case INTEG_RAINMAKER: lv_label_set_text(ui_LabelColorTemModeScreenLight, "Rainmaker"); break;
    }
    lv_obj_set_style_text_color(ui_LabelColorTemModeScreenLight, on?txt_on:gray, LV_PART_MAIN|LV_STATE_DEFAULT);
}

static void light_fullscreen_nolock(void) {
    if (!ui_ImageColorTemScreenLight) return;
    lv_display_t* d = lv_display_get_default();
    if (!d) return;
    int w = lv_display_get_horizontal_resolution(d);
    int h = lv_display_get_vertical_resolution(d);
    ESP_LOGI(TAG, "set light bg to full screen %dx%d", w, h);
    lv_obj_set_size(ui_ImageColorTemScreenLight, w, h);
    lv_obj_set_align(ui_ImageColorTemScreenLight, LV_ALIGN_TOP_LEFT);
}

static void aircon_fullscreen_nolock(void) {
    if (!ui_ImageScreenAirCon) return;
    lv_display_t* d = lv_display_get_default();
    if (!d) return;
    int w = lv_display_get_horizontal_resolution(d);
    int h = lv_display_get_vertical_resolution(d);
    ESP_LOGI(TAG, "set aircon bg to full screen %dx%d", w, h);
    lv_obj_set_size(ui_ImageScreenAirCon, w, h);
    lv_obj_set_align(ui_ImageScreenAirCon, LV_ALIGN_TOP_LEFT);
}

static void music_fullscreen_nolock(void) {
    if (!ui_ImageScreenMusic) return;
    lv_display_t* d = lv_display_get_default();
    if (!d) return;
    int w = lv_display_get_horizontal_resolution(d);
    int h = lv_display_get_vertical_resolution(d);
    ESP_LOGI(TAG, "set music bg to full screen %dx%d", w, h);
    lv_obj_set_size(ui_ImageScreenMusic, w, h);
    lv_obj_set_align(ui_ImageScreenMusic, LV_ALIGN_TOP_LEFT);
}

static void music_apply_nolock(void) {
    if (!ui_ScreenMusic) return;
    
    if (!s_state.music_rhythm_initialized) {
        rhythm_visualizer_init();
        rhythm_effect_config_t config = {
            .sensitivity = 6,
            .speed = 5,
            .brightness = 200,
            .smooth_mode = true
        };
        rhythm_set_effect_config(&config);
        s_state.music_rhythm_initialized = true;
    }
    
    const lv_image_dsc_t* imgs[3] = { &fire, &rain, &sea };
    const char* audio_files[3] = {"/spiffs/fire.wav", "/spiffs/rain.wav", "/spiffs/sea.wav"};
    rhythm_scene_t scenes[3] = {RHYTHM_SCENE_FIRE, RHYTHM_SCENE_RAIN, RHYTHM_SCENE_WAVE};
    
    int idx = (s_state.music_index % 3 + 3) % 3;
    lv_image_set_src(ui_ImageScreenMusic, imgs[idx]);
    
    if (s_state.music_playing) {
        if (rhythm_is_running()) {
            rhythm_stop();
        }
        rhythm_start_natural_sound(scenes[idx], audio_files[idx]);
        ESP_LOGI(TAG, "Playing %s with rhythm scene %d", audio_files[idx], scenes[idx]);
    } else {
        if (rhythm_is_running()) {
            rhythm_stop();
        }
    }
}

static void aircon_apply_nolock(void) {
    if (!ui_ScreenAirCon) return;
    int minv = AIR_TEMP_MIN, maxv = AIR_TEMP_MAX;
    lv_color_t blue = lv_color_hex(0x00BFFF);
    lv_color_t red = lv_color_hex(0xFF4D4D);
    lv_color_t gray = lv_color_hex(0x808080);
    lv_color_t txt_on = lv_color_hex(0x333333);
    bool on = s_state.air_on;
    lv_obj_set_style_arc_color(ui_ArcScreenAirCon, on ? (s_state.air_mode==AIR_COOL?blue:red) : gray, LV_PART_INDICATOR|LV_STATE_DEFAULT);
    lv_arc_set_range(ui_ArcScreenAirCon, minv, maxv);
    lv_arc_set_value(ui_ArcScreenAirCon, s_state.air_temp);
    lv_label_set_text(ui_LabelModeScreenAirCon, s_state.air_mode==AIR_COOL?"COOL":"HEAT");
    char tbuf[8];
    snprintf(tbuf,sizeof(tbuf),"%d℃", s_state.air_temp);
    lv_label_set_text(ui_LabeModeRatioScreenAirCon, tbuf);
    const char* bname = s_state.air_brand==BRAND_MIDEA?"Midea":(s_state.air_brand==BRAND_GREE?"Gree":"Haier");
    lv_label_set_text(ui_LabelColorTemBrandScreenAirCon, bname);
    lv_obj_set_style_text_color(ui_LabelModeScreenAirCon, on?txt_on:gray, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_LabeModeRatioScreenAirCon, on?txt_on:gray, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_LabelModeDecScreenAirCon, on?txt_on:gray, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_LabelModeAddScreenAirCon, on?txt_on:gray, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_LabelModeCheckScreenAirCon, on?txt_on:gray, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_LabelModeOnOffScreenAirCon, on?txt_on:gray, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui_LabelColorTemBrandScreenAirCon, on?txt_on:gray, LV_PART_MAIN|LV_STATE_DEFAULT);
}

static void show_screen_nolock(const char* name) {
    if (!name) return;
    ESP_LOGI(TAG, "ui_extra_show_screen: %s", name);
    
    if (s_state.page == PAGE_MUSIC && strcmp(name, "music") != 0) {
        if (rhythm_is_running()) {
            rhythm_stop();
        }
        if (s_state.music_rhythm_initialized) {
            rhythm_visualizer_deinit();
            s_state.music_rhythm_initialized = false;
        }
    }
    if (strcmp(name, "light") == 0) {
        lv_disp_load_scr(ui_ScreenLight);
        s_state.page = PAGE_LIGHT;
        light_fullscreen_nolock();
        light_apply_nolock();
    } else if (strcmp(name, "aircon") == 0) {
        lv_disp_load_scr(ui_ScreenAirCon);
        s_state.page = PAGE_AIRCON;
        aircon_fullscreen_nolock();
        aircon_apply_nolock();
        ir_ac_init();
        ir_brand_t b = (s_state.air_brand==BRAND_MIDEA?IR_BRAND_MIDEA:(s_state.air_brand==BRAND_GREE?IR_BRAND_GREE:IR_BRAND_HAIER));
        ir_ac_set_brand(b);
        ir_ac_apply_async(s_state.air_on, s_state.air_mode==AIR_COOL, s_state.air_temp); 
    } else if (strcmp(name, "music") == 0) {
        lv_disp_load_scr(ui_ScreenMusic);
        s_state.page = PAGE_MUSIC;
        music_fullscreen_nolock();
        music_apply_nolock();
    } else if (strcmp(name, "keyboard") == 0) {
        lv_disp_load_scr(ui_ScreenKeyBoard);
        s_state.page = PAGE_KEYBOARD;
    }
}
void ui_extra_init(void) {
    if (g_inited) return;
    ESP_LOGI(TAG, "ui_extra_init");
    g_inited = true;
    s_state.page = PAGE_LIGHT;
    light_fullscreen_nolock();
    light_apply_nolock();
    bsp_wav_init_async();
}
void ui_extra_set_status(const char* text) {
    if (!g_inited) return;
    ESP_LOGI(TAG, "ui_extra_set_status: %s", text ? text : "");
}
void ui_extra_show_screen(const char* name) {
    if (!g_inited) return;
    if (bsp_display_lock(0)) {
        show_screen_nolock(name);
        bsp_display_unlock();
    }
}

void ui_extra_on_button(bsp_button_source_t source, bsp_button_event_t event) {
    if (!g_inited) return;
    if (bsp_display_lock(0)) {
        if (s_state.page == PAGE_KEYBOARD) {
            if (event == BSP_BUTTON_EVENT_LONG_PRESS) {
                if (source == BSP_INPUT_TOUCH_LEFT) {
                    show_screen_nolock("music");
                } else if (source == BSP_INPUT_TOUCH_RIGHT) {
                    show_screen_nolock("light");
                }
                bsp_display_unlock();
                return;
            }
            if (event == BSP_BUTTON_EVENT_PRESS_DOWN || event == BSP_BUTTON_EVENT_PRESS_UP) {
                lv_obj_t* btn = NULL;
                uint32_t color = 0;
                const char* path = NULL;
                switch (source) {
                    case BSP_INPUT_TOUCH_TOP_LEFT: btn = ui_ButtonScreenKeyBoardDo; color = 0xFF3B30; path = BSP_SPIFFS_MOUNT_POINT "/do.wav"; break;
                    case BSP_INPUT_TOUCH_LEFT: btn = ui_ButtonScreenKeyBoardRe; color = 0xFF9500; path = BSP_SPIFFS_MOUNT_POINT "/re.wav"; break;
                    case BSP_INPUT_TOUCH_BOTTOM_LEFT: btn = ui_ButtonScreenKeyBoardMi; color = 0xFFCC00; path = BSP_SPIFFS_MOUNT_POINT "/mi.wav"; break;
                    case BSP_INPUT_TOUCH_TOP_RIGHT: btn = ui_ButtonScreenKeyBoardFa; color = 0x34C759; path = BSP_SPIFFS_MOUNT_POINT "/fa.wav"; break;
                    case BSP_INPUT_TOUCH_RIGHT: btn = ui_ButtonScreenKeyBoardSo; color = 0x5AC8FA; path = BSP_SPIFFS_MOUNT_POINT "/so.wav"; break;
                    case BSP_INPUT_TOUCH_BOTTOM_RIGHT: btn = ui_ButtonScreenKeyBoardLa; color = 0x007AFF; path = BSP_SPIFFS_MOUNT_POINT "/la.wav"; break;
                    default: break;
                }
                if (btn) {
                    if (event == BSP_BUTTON_EVENT_PRESS_DOWN) {
                        lv_obj_add_state(btn, LV_STATE_FOCUSED);
                        bsp_led_set_for_touch(source, color);
                        if (path) {
                            bsp_wav_play_file_async(path);
                        }
                        if (s_kb_audio_timer) esp_timer_stop(s_kb_audio_timer);
                    } else {
                        lv_obj_clear_state(btn, LV_STATE_FOCUSED);
                        bsp_led_clear_for_touch(source);
                        if (!s_kb_audio_timer) {
                            esp_timer_create_args_t args = {0};
                            args.callback = kb_audio_timer_cb;
                            args.name = "kb_audio_close";
                            esp_timer_create(&args, &s_kb_audio_timer);
                        }
                        esp_timer_stop(s_kb_audio_timer);
                        esp_timer_start_once(s_kb_audio_timer, 300000);
                    }
                }
                bsp_display_unlock();
                return;
            }
            bsp_display_unlock();
            return;
        }
        if (event == BSP_BUTTON_EVENT_LONG_PRESS) {
            if (s_state.page == PAGE_LIGHT && source == BSP_INPUT_TOUCH_BOTTOM_LEFT) {
                s_state.bl_longpress = true;
                if (s_state.integration == INTEG_MATTER) s_state.integration = INTEG_HA; else if (s_state.integration == INTEG_HA) s_state.integration = INTEG_RAINMAKER; else s_state.integration = INTEG_MATTER;
                ESP_LOGI(TAG, "integration mode: %d", s_state.integration);
                light_apply_nolock();
            } else if (s_state.page == PAGE_AIRCON && source == BSP_INPUT_TOUCH_BOTTOM_LEFT) {
                s_state.ac_bl_longpress = true;
                if (s_state.air_brand == BRAND_MIDEA) s_state.air_brand = BRAND_GREE; else if (s_state.air_brand == BRAND_GREE) s_state.air_brand = BRAND_HAIER; else s_state.air_brand = BRAND_MIDEA;
                ESP_LOGI(TAG, "aircon brand: %d", s_state.air_brand);
                aircon_apply_nolock();
                ir_brand_t b = (s_state.air_brand==BRAND_MIDEA?IR_BRAND_MIDEA:(s_state.air_brand==BRAND_GREE?IR_BRAND_GREE:IR_BRAND_HAIER));
                ir_ac_set_brand(b);
            }
            bsp_display_unlock();
            return;
        }
        if (event != BSP_BUTTON_EVENT_PRESS_UP) { bsp_display_unlock(); return; }
        if (source == BSP_INPUT_TOUCH_LEFT) {
            if (s_state.page==PAGE_LIGHT) show_screen_nolock("keyboard");
            else if (s_state.page==PAGE_AIRCON) show_screen_nolock("light");
            else if (s_state.page==PAGE_MUSIC) show_screen_nolock("aircon");
            else if (s_state.page==PAGE_KEYBOARD) show_screen_nolock("music");
            bsp_display_unlock();
            return;
        } else if (source == BSP_INPUT_TOUCH_RIGHT) {
            if (s_state.page==PAGE_LIGHT) show_screen_nolock("aircon");
            else if (s_state.page==PAGE_AIRCON) show_screen_nolock("music");
            else if (s_state.page==PAGE_MUSIC) show_screen_nolock("keyboard");
            else if (s_state.page==PAGE_KEYBOARD) show_screen_nolock("light");
            bsp_display_unlock();
            return;
        }
        if (s_state.page == PAGE_LIGHT) {
        switch (source) {
            case BSP_INPUT_TOUCH_TOP_LEFT:
                if (!s_state.light_on) { ESP_LOGI(TAG, "light is off, ignore value/mode changes"); break; }
                if (s_state.mode==MODE_HUE) s_state.hue = (s_state.hue+360-HUE_STEP)%360; else s_state.brightness = s_state.brightness>BRI_MIN? s_state.brightness-BRI_STEP:BRI_MIN;
                ESP_LOGI(TAG, "light TL: hue=%d bri=%d", s_state.hue, s_state.brightness);
                light_apply_nolock();
                break;
            case BSP_INPUT_TOUCH_TOP_RIGHT:
                if (!s_state.light_on) { ESP_LOGI(TAG, "light is off, ignore value/mode changes"); break; }
                if (s_state.mode==MODE_HUE) s_state.hue = (s_state.hue+HUE_STEP)%360; else s_state.brightness = s_state.brightness<BRI_MAX? s_state.brightness+BRI_STEP:BRI_MAX;
                ESP_LOGI(TAG, "light TR: hue=%d bri=%d", s_state.hue, s_state.brightness);
                light_apply_nolock();
                break;
            case BSP_INPUT_TOUCH_BOTTOM_LEFT:
                if (s_state.bl_longpress) { s_state.bl_longpress = false; ESP_LOGI(TAG, "skip mode toggle after long press"); break; }
                if (!s_state.light_on) { ESP_LOGI(TAG, "light is off, ignore value/mode changes"); break; }
                s_state.mode = (s_state.mode==MODE_HUE)? MODE_BRIGHTNESS: MODE_HUE;
                ESP_LOGI(TAG, "light toggle mode: %d", s_state.mode);
                light_apply_nolock();
                break;
            case BSP_INPUT_TOUCH_BOTTOM_RIGHT:
                s_state.light_on = !s_state.light_on;
                ESP_LOGI(TAG, "light onoff: %d", s_state.light_on);
                light_apply_nolock();
                break;
            default: break;
        }
        } else if (s_state.page == PAGE_AIRCON) {
        switch (source) {
            case BSP_INPUT_TOUCH_TOP_LEFT:
                if (!s_state.air_on) { ESP_LOGI(TAG, "aircon is off, ignore value changes"); break; }
                if (s_state.air_temp > AIR_TEMP_MIN) s_state.air_temp -= AIR_TEMP_STEP;
                ESP_LOGI(TAG, "aircon TL: temp=%d", s_state.air_temp);
                aircon_apply_nolock();
                ir_ac_set_mode_and_temp_async(s_state.air_mode==AIR_COOL, s_state.air_temp);
                break;
            case BSP_INPUT_TOUCH_TOP_RIGHT:
                if (!s_state.air_on) { ESP_LOGI(TAG, "aircon is off, ignore value changes"); break; }
                if (s_state.air_temp < AIR_TEMP_MAX) s_state.air_temp += AIR_TEMP_STEP;
                ESP_LOGI(TAG, "aircon TR: temp=%d", s_state.air_temp);
                aircon_apply_nolock();
                ir_ac_set_mode_and_temp_async(s_state.air_mode==AIR_COOL, s_state.air_temp);
                break;
            case BSP_INPUT_TOUCH_BOTTOM_LEFT:
                if (s_state.ac_bl_longpress) { s_state.ac_bl_longpress = false; ESP_LOGI(TAG, "skip aircon mode toggle after long press"); break; }
                if (!s_state.air_on) { ESP_LOGI(TAG, "aircon is off, ignore mode changes"); break; }
                s_state.air_mode = (s_state.air_mode==AIR_COOL)? AIR_HEAT: AIR_COOL;
                ESP_LOGI(TAG, "aircon toggle mode: %d", s_state.air_mode);
                aircon_apply_nolock();
                ir_ac_set_mode_and_temp_async(s_state.air_mode==AIR_COOL, s_state.air_temp);
                break;
            case BSP_INPUT_TOUCH_BOTTOM_RIGHT:
                s_state.air_on = !s_state.air_on;
                ESP_LOGI(TAG, "aircon onoff: %d", s_state.air_on);
                aircon_apply_nolock();
                ir_ac_set_power_async(s_state.air_on);
                break;
            default: break;
        }
        } else if (s_state.page == PAGE_MUSIC) {
        switch (source) {
            case BSP_INPUT_TOUCH_TOP_LEFT:
                s_state.music_index = (s_state.music_index + 3 - 1) % 3;
                ESP_LOGI(TAG, "Music: previous");
                music_apply_nolock();
                break;
            case BSP_INPUT_TOUCH_TOP_RIGHT:
                s_state.music_index = (s_state.music_index + 1) % 3;
                ESP_LOGI(TAG, "Music: next");
                music_apply_nolock();
                break;
            case BSP_INPUT_TOUCH_BOTTOM_LEFT:
                s_state.music_playing = !s_state.music_playing;
                if (s_state.music_playing) {
                    ESP_LOGI(TAG, "Music: resumed");
                } else {
                    ESP_LOGI(TAG, "Music: paused");
                }
                music_apply_nolock();
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
        if (s_state.page == PAGE_LIGHT) {
            light_fullscreen_nolock();
            light_apply_nolock();
        } else if (s_state.page == PAGE_AIRCON) {
            aircon_fullscreen_nolock();
            aircon_apply_nolock();
        } else if (s_state.page == PAGE_MUSIC) {
            show_screen_nolock("music");
        } else if (s_state.page == PAGE_KEYBOARD) {
            show_screen_nolock("keyboard");
        }
        bsp_display_unlock();
    }
}


