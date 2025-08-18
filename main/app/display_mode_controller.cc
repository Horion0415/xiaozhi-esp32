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
}


