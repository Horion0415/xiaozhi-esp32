#include "ir_ac_bridge.h"
#include "ir_ac_controller.h"

extern "C" {

void ir_ac_init(void) {
    IrAcController::Instance().Init();
}

void ir_ac_set_brand(ir_brand_t brand) {
    IrAcController::Instance().SetBrand(brand);
}

void ir_ac_set_power(bool on) {
    IrAcController::Instance().SetPower(on);
}

void ir_ac_set_mode_and_temp(bool cool_mode, uint8_t temperature) {
    IrAcController::Instance().SetModeAndTemp(cool_mode, temperature);
}

void ir_ac_apply(bool on, bool cool_mode, uint8_t temperature) {
    IrAcController::Instance().Apply(on, cool_mode, temperature);
}

void ir_ac_set_model_name(const char* name) {
    IrAcController::Instance().SetModelName(name);
}

}


