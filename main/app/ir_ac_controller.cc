#include "ir_ac_controller.h"
#include <cstring>
#include <cstdio>

IrAcController& IrAcController::Instance() {
    static IrAcController inst;
    return inst;
}

void IrAcController::Init() {
    if (inited_) return;
    if (!bsp_ir_is_initialized()) {
        if (bsp_ir_init() != ESP_OK) return;
    }
    inited_ = true;
    EnsureWorker();
}

void IrAcController::SetBrand(ir_brand_t brand) {
    brand_ = brand;
    switch (brand) {
        case IR_BRAND_MIDEA: brand_name_ = "美的"; model_id_ = 3387;  strncpy(model_name_, "3387", sizeof(model_name_) - 1); break;
        case IR_BRAND_HAIER: brand_name_ = "海尔"; model_id_ = 77;  strncpy(model_name_, "77", sizeof(model_name_) - 1); break;
        case IR_BRAND_GREE:  brand_name_ = "格力";  model_id_ = 10020; strncpy(model_name_, "10020", sizeof(model_name_) - 1); break;
        default: brand_name_ = "美的"; model_id_ = 3387; strncpy(model_name_, "3387", sizeof(model_name_) - 1); break;
    }
}

void IrAcController::Apply(bool power_on, bool cool_mode, uint8_t temperature) {
    Init();
    if (!inited_) return;
    if (temperature < 16) temperature = 16;
    if (temperature > 30) temperature = 30;
    ir_device_info_t info = {};
    info.category = IR_DEVICE_AC;
    strncpy(info.brand, brand_name_, IR_MAX_BRAND_NAME_LEN - 1);
    info.model_id = model_id_;
    strncpy(info.model, model_name_[0] ? model_name_ : "-", IR_MAX_MODEL_NAME_LEN - 1);
    ir_ac_status_t st = {};
    st.power = power_on ? IR_AC_POWER_ON : IR_AC_POWER_OFF;
    st.mode = cool_mode ? IR_AC_MODE_COOL : IR_AC_MODE_HEAT;
    st.temperature = temperature;
    st.wind_speed = IR_AC_WIND_AUTO;
    st.swing = IR_AC_SWING_OFF;
    EnsureWorker();
    AcMessage msg{};
    msg.info = info;
    msg.ac = st;
    esp_err_t res = ESP_FAIL;
    msg.result = &res;
    msg.waiter = xTaskGetCurrentTaskHandle();
    if (queue_ && xQueueSend(queue_, &msg, pdMS_TO_TICKS(1000)) == pdTRUE) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
    power_on_ = power_on;
    cool_mode_ = cool_mode;
    temperature_ = temperature;
}

void IrAcController::SetPower(bool on) {
    Apply(on, cool_mode_, temperature_);
}

void IrAcController::SetModeAndTemp(bool cool_mode, uint8_t temperature) {
    Apply(true, cool_mode, temperature);
}

void IrAcController::SetModelName(const char* name) {
    if (!name) return;
    strncpy(model_name_, name, sizeof(model_name_) - 1);
    // Force online resolution next time by clearing cached model_id
    model_id_ = 0;
}

void IrAcController::Worker(void* arg) {
    IrAcController* self = static_cast<IrAcController*>(arg);
    AcMessage msg{};
    for (;;) {
        if (xQueueReceive(self->queue_, &msg, portMAX_DELAY) == pdTRUE) {
            ir_device_info_t info = msg.info;
            ir_ac_status_t st = msg.ac;
            esp_err_t r = ir_send_ac_command(&info, &st);
            if (r != ESP_OK) {
                // If send failed, try to resolve a valid model_id online and retry once
                const char* brand_for_api = nullptr;
                switch (self->brand_) {
                    case IR_BRAND_MIDEA: brand_for_api = "美的"; break;
                    case IR_BRAND_HAIER: brand_for_api = "海尔"; break;
                    case IR_BRAND_GREE:  brand_for_api = "格力";  break;
                    default: brand_for_api = info.brand[0] ? info.brand : "美的"; break;
                }

                // 1) If we have a model name, try to resolve by brand+model
                if (r != ESP_OK && info.model[0] && strcmp(info.model, "-") != 0) {
                    uint32_t bid = 0, mid = 0;
                    if (ir_find_device_by_name(IR_DEVICE_AC, brand_for_api, info.model, &bid, &mid) == ESP_OK && mid != 0) {
                        info.model_id = mid;
                        r = ir_send_ac_command(&info, &st);
                        if (r == ESP_OK) {
                            // Persist resolved id
                            self->model_id_ = mid;
                        }
                    }
                }

                // 2) If still failing or we had no model name, pick the first supported model as a fallback
                if (r != ESP_OK) {
                    char models[1][IR_MAX_MODEL_NAME_LEN];
                    size_t found = 0;
                    if (ir_get_supported_models(IR_DEVICE_AC, brand_for_api, models, 1, &found) == ESP_OK && found > 0) {
                        uint32_t bid = 0, mid = 0;
                        if (ir_find_device_by_name(IR_DEVICE_AC, brand_for_api, models[0], &bid, &mid) == ESP_OK && mid != 0) {
                            // Update controller cache
                            strncpy(self->model_name_, models[0], sizeof(self->model_name_) - 1);
                            self->model_id_ = mid;
                            // Update current request
                            info.model_id = mid;
                            strncpy(info.model, models[0], IR_MAX_MODEL_NAME_LEN - 1);
                            r = ir_send_ac_command(&info, &st);
                        }
                    }
                }
            }
            if (msg.result) *msg.result = r;
            if (msg.waiter) xTaskNotifyGive(msg.waiter);
        }
    }
}

void IrAcController::EnsureWorker() {
    if (task_ && queue_) return;
    if (!queue_) queue_ = xQueueCreate(8, sizeof(AcMessage));
    if (!task_) {
        if (!tcb_) tcb_ = (StaticTask_t*)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
        if (!stack_) stack_ = (StackType_t*)heap_caps_malloc(25600, MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if (tcb_ && stack_) task_ = xTaskCreateStatic(Worker, "ir_worker", 25600/sizeof(StackType_t), this, 5, stack_, tcb_);
    }
}


