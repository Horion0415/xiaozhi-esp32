#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    IR_BRAND_MIDEA = 0,
    IR_BRAND_HAIER,
    IR_BRAND_GREE,
} ir_brand_t;

void ir_ac_init(void);
void ir_ac_set_brand(ir_brand_t brand);
void ir_ac_set_power(bool on);
void ir_ac_set_power_async(bool on);
void ir_ac_set_mode_and_temp(bool cool_mode, uint8_t temperature);
void ir_ac_set_mode_and_temp_async(bool cool_mode, uint8_t temperature);
void ir_ac_apply(bool on, bool cool_mode, uint8_t temperature);
void ir_ac_apply_async(bool on, bool cool_mode, uint8_t temperature);
void ir_ac_set_model_name(const char* name);

#ifdef __cplusplus
}
#endif


