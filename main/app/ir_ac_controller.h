#pragma once
#include <stdint.h>
#include "ir_remote_control.h"
#include "bsp/esp32_c5_sensairpanel.h"
#include "ir_ac_bridge.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

class IrAcController {
public:
    static IrAcController& Instance();
    void Init();
    void SetBrand(ir_brand_t brand);
    void Apply(bool power_on, bool cool_mode, uint8_t temperature);
    void ApplyAsync(bool power_on, bool cool_mode, uint8_t temperature);  // 异步版本，不等待完成
    void SetPower(bool on);
    void SetPowerAsync(bool on);
    void SetModeAndTemp(bool cool_mode, uint8_t temperature);
    void SetModeAndTempAsync(bool cool_mode, uint8_t temperature);
    void SetModelName(const char* name);
private:
    IrAcController() = default;
    struct AcMessage {
        ir_device_info_t info;
        ir_ac_status_t ac;
        TaskHandle_t waiter;
        esp_err_t* result;
    };
    static void Worker(void* arg);
    void EnsureWorker();
    ir_brand_t brand_ = IR_BRAND_MIDEA;
    const char* brand_name_ = "Midea";
    uint32_t model_id_ = 35;
    char model_name_[IR_MAX_MODEL_NAME_LEN] = {0};
    bool inited_ = false;
    bool power_on_ = true;
    bool cool_mode_ = true;
    uint8_t temperature_ = 26;
    QueueHandle_t queue_ = nullptr;
    TaskHandle_t task_ = nullptr;
    StaticTask_t* tcb_ = nullptr;
    StackType_t* stack_ = nullptr;
};


