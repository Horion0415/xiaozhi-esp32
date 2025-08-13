#include "display_mode_controller.h"
#include "bsp/esp32_c5_sensairpanel.h"

DisplayModeController::DisplayModeController(lv_display_t* disp) : disp_(disp) {}

void DisplayModeController::EnterGfxMode()
{
    if (!disp_) return;
    if (lvgl_port_take_trans_sem(disp_, portMAX_DELAY) != ESP_OK) return;
    ESP_ERROR_CHECK_WITHOUT_ABORT(lvgl_port_set_dummy_draw(disp_, true));
    ESP_ERROR_CHECK_WITHOUT_ABORT(lvgl_port_give_trans_sem(disp_, false));
}

void DisplayModeController::EnterLvglMode()
{
    if (!disp_) return;
    if (lvgl_port_take_trans_sem(disp_, portMAX_DELAY) != ESP_OK) return;
    ESP_ERROR_CHECK_WITHOUT_ABORT(lvgl_port_set_dummy_draw(disp_, false));
    ESP_ERROR_CHECK_WITHOUT_ABORT(lvgl_port_give_trans_sem(disp_, false));
    if (bsp_display_lock(0)) {
        lv_obj_t* scr = lv_screen_active();
        lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
        if (!overlay_box_) {
            overlay_box_ = lv_obj_create(scr);
            lv_obj_set_size(overlay_box_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_style_bg_opa(overlay_box_, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(overlay_box_, lv_color_hex(0x202020), 0);
            lv_obj_set_style_border_opa(overlay_box_, LV_OPA_TRANSP, 0);
            lv_obj_set_style_radius(overlay_box_, 8, 0);
            lv_obj_set_style_pad_all(overlay_box_, 6, 0);
            lv_obj_align(overlay_box_, LV_ALIGN_CENTER, 0, 0);
            label_ = lv_label_create(overlay_box_);
            lv_obj_set_style_text_color(label_, lv_color_white(), 0);
        }
        char buf[32];
        ++lvgl_switch_count_;
        snprintf(buf, sizeof(buf), "LVGL:%d", lvgl_switch_count_);
        lv_label_set_text(label_, buf);
        lv_obj_align(overlay_box_, LV_ALIGN_CENTER, 0, 0);
        lv_obj_move_foreground(overlay_box_);
        lv_obj_invalidate(scr);
        bsp_display_unlock();
    }
}


