#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "protocol_examples_common.h"
#include "driver/gpio.h"
#include "unity.h"

#include "ir_remote_control.h"
#include "irext_api.h"

static const char *TAG = "ir_test";

#define IR_TEST_TX_GPIO 25
#define IR_TEST_INVERT_SIGNAL false

static inline uint32_t get_carrier_hz(void)
{
#if CONFIG_IR_TEST_CARRIER_56KHZ
    return 56000;
#else
    return 38000;
#endif
}

static bool resolve_device_info(ir_device_category_t category, ir_device_info_t *out)
{
    char brands[10][IR_MAX_BRAND_NAME_LEN]; size_t brand_cnt = 0;
    if (ir_get_supported_brands(category, brands, 10, &brand_cnt) != ESP_OK || brand_cnt == 0) {
        ESP_LOGW(TAG, "No brands for category %d", (int)category);
        return false;
    }
    char models[10][IR_MAX_MODEL_NAME_LEN]; size_t model_cnt = 0;
    if (ir_get_supported_models(category, brands[0], models, 10, &model_cnt) != ESP_OK || model_cnt == 0) {
        ESP_LOGW(TAG, "No models for %s in cat %d", brands[0], (int)category);
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->category = category;
    snprintf(out->brand, sizeof(out->brand), "%s", brands[0]);
    snprintf(out->model, sizeof(out->model), "%s", models[0]);
    return true;
}

// -------- Unity test cases per device category --------

static void test_tv(void)
{
    ir_device_info_t tv;
    if (!resolve_device_info(IR_DEVICE_TV, &tv)) return;
    esp_err_t r;
    r = ir_send_tv_key(&tv, IR_TV_KEY_POWER); if (r == ESP_ERR_NO_MEM) { vTaskDelay(pdMS_TO_TICKS(50)); r = ir_send_tv_key(&tv, IR_TV_KEY_POWER); } TEST_ASSERT_EQUAL(ESP_OK, r);
    r = ir_send_tv_key(&tv, IR_TV_KEY_VOL_UP); if (r == ESP_ERR_NO_MEM) { vTaskDelay(pdMS_TO_TICKS(50)); r = ir_send_tv_key(&tv, IR_TV_KEY_VOL_UP); } TEST_ASSERT_EQUAL(ESP_OK, r);
    r = ir_send_tv_key(&tv, IR_TV_KEY_MUTE); if (r == ESP_ERR_NO_MEM) { vTaskDelay(pdMS_TO_TICKS(50)); r = ir_send_tv_key(&tv, IR_TV_KEY_MUTE); } TEST_ASSERT_EQUAL(ESP_OK, r);
    TEST_ASSERT_EQUAL(ESP_OK, ir_send_tv_digit(&tv, 1));
}

static void test_stb(void)
{
    ir_province_t provinces[8]; size_t prov_cnt=0;
    if (irext_api_list_provinces(provinces, 8, &prov_cnt) != ESP_OK || prov_cnt == 0) return;
    ir_city_t cities[8]; size_t city_cnt=0;
    if (irext_api_list_cities(provinces[0].code, cities, 8, &city_cnt) != ESP_OK || city_cnt == 0) return;
    ir_operator_t ops[8]; size_t op_cnt=0;
    (void)irext_api_list_operators(cities[0].code, ops, 8, &op_cnt);
    uint32_t index_ids[8]; size_t idx_cnt=0;
    if (irext_api_list_indexes_by_city(cities[0].code, index_ids, 8, &idx_cnt) != ESP_OK || idx_cnt == 0) return;
    ir_device_info_t dev = {0};
    dev.category = IR_DEVICE_STB;
    dev.model_id = index_ids[0];
    TEST_ASSERT_EQUAL(ESP_OK, ir_send_key_command(&dev, (uint32_t)IR_STB_KEY_POWER));
}

static void test_box(void)
{
    ir_device_info_t box;
    if (!resolve_device_info(IR_DEVICE_BOX, &box)) return;
    esp_err_t r;
    r = ir_send_box_key(&box, IR_BOX_KEY_POWER); if (r == ESP_ERR_NO_MEM) { vTaskDelay(pdMS_TO_TICKS(50)); r = ir_send_box_key(&box, IR_BOX_KEY_POWER); } TEST_ASSERT_EQUAL(ESP_OK, r);
    r = ir_send_box_key(&box, IR_BOX_KEY_HOME); if (r == ESP_ERR_NO_MEM) { vTaskDelay(pdMS_TO_TICKS(50)); r = ir_send_box_key(&box, IR_BOX_KEY_HOME); } TEST_ASSERT_EQUAL(ESP_OK, r);
}

static void test_iptv(void)
{
    ir_device_info_t iptv;
    if (!resolve_device_info(IR_DEVICE_IPTV, &iptv)) return;
    esp_err_t r;
    r = ir_send_iptv_key(&iptv, IR_IPTV_KEY_POWER); if (r == ESP_ERR_NO_MEM) { vTaskDelay(pdMS_TO_TICKS(50)); r = ir_send_iptv_key(&iptv, IR_IPTV_KEY_POWER); } TEST_ASSERT_EQUAL(ESP_OK, r);
    r = ir_send_iptv_key(&iptv, IR_IPTV_KEY_MENU); if (r == ESP_ERR_NO_MEM) { vTaskDelay(pdMS_TO_TICKS(50)); r = ir_send_iptv_key(&iptv, IR_IPTV_KEY_MENU); } TEST_ASSERT_EQUAL(ESP_OK, r);
}

static void test_dvd(void)
{
    ir_device_info_t dvd;
    if (!resolve_device_info(IR_DEVICE_DVD, &dvd)) return;
    esp_err_t r;
    r = ir_send_dvd_key(&dvd, IR_DVD_KEY_POWER); if (r == ESP_ERR_NO_MEM) { vTaskDelay(pdMS_TO_TICKS(50)); r = ir_send_dvd_key(&dvd, IR_DVD_KEY_POWER); } TEST_ASSERT_EQUAL(ESP_OK, r);
    r = ir_send_dvd_key(&dvd, IR_DVD_KEY_PLAY); if (r == ESP_ERR_NO_MEM) { vTaskDelay(pdMS_TO_TICKS(50)); r = ir_send_dvd_key(&dvd, IR_DVD_KEY_PLAY); } TEST_ASSERT_EQUAL(ESP_OK, r);
}

static void test_fan(void)
{
    ir_device_info_t fan;
    if (!resolve_device_info(IR_DEVICE_FAN, &fan)) return;
    esp_err_t r;
    r = ir_send_fan_key(&fan, IR_FAN_KEY_POWER); if (r == ESP_ERR_NO_MEM) { vTaskDelay(pdMS_TO_TICKS(50)); r = ir_send_fan_key(&fan, IR_FAN_KEY_POWER); } TEST_ASSERT_EQUAL(ESP_OK, r);
    r = ir_send_fan_key(&fan, IR_FAN_KEY_SWING); if (r == ESP_ERR_NO_MEM) { vTaskDelay(pdMS_TO_TICKS(50)); r = ir_send_fan_key(&fan, IR_FAN_KEY_SWING); } TEST_ASSERT_EQUAL(ESP_OK, r);
}

static void test_projector(void)
{
    ir_device_info_t proj;
    if (!resolve_device_info(IR_DEVICE_PROJECTOR, &proj)) return;
    esp_err_t r;
    r = ir_send_projector_key(&proj, IR_PROJECTOR_KEY_POWER); if (r == ESP_ERR_NO_MEM) { vTaskDelay(pdMS_TO_TICKS(50)); r = ir_send_projector_key(&proj, IR_PROJECTOR_KEY_POWER); } TEST_ASSERT_EQUAL(ESP_OK, r);
    r = ir_send_projector_key(&proj, IR_PROJECTOR_KEY_MENU); if (r == ESP_ERR_NO_MEM) { vTaskDelay(pdMS_TO_TICKS(50)); r = ir_send_projector_key(&proj, IR_PROJECTOR_KEY_MENU); } TEST_ASSERT_EQUAL(ESP_OK, r);
}

static void test_stereo(void)
{
    ir_device_info_t stereo;
    if (!resolve_device_info(IR_DEVICE_STEREO, &stereo)) return;
    esp_err_t r;
    r = ir_send_stereo_key(&stereo, IR_STEREO_KEY_POWER); if (r == ESP_ERR_NO_MEM) { vTaskDelay(pdMS_TO_TICKS(50)); r = ir_send_stereo_key(&stereo, IR_STEREO_KEY_POWER); } TEST_ASSERT_EQUAL(ESP_OK, r);
    r = ir_send_stereo_key(&stereo, IR_STEREO_KEY_MUTE); if (r == ESP_ERR_NO_MEM) { vTaskDelay(pdMS_TO_TICKS(50)); r = ir_send_stereo_key(&stereo, IR_STEREO_KEY_MUTE); } TEST_ASSERT_EQUAL(ESP_OK, r);
}

static void test_bulb(void)
{
    ir_device_info_t bulb;
    if (!resolve_device_info(IR_DEVICE_BULB, &bulb)) return;
    esp_err_t r;
    r = ir_send_bulb_key(&bulb, IR_BULB_KEY_POWER); if (r == ESP_ERR_NO_MEM) { vTaskDelay(pdMS_TO_TICKS(50)); r = ir_send_bulb_key(&bulb, IR_BULB_KEY_POWER); } TEST_ASSERT_EQUAL(ESP_OK, r);
    r = ir_send_bulb_key(&bulb, IR_BULB_KEY_BRIGHT_UP); if (r == ESP_ERR_NO_MEM) { vTaskDelay(pdMS_TO_TICKS(50)); r = ir_send_bulb_key(&bulb, IR_BULB_KEY_BRIGHT_UP); } TEST_ASSERT_EQUAL(ESP_OK, r);
}

static void test_robot(void)
{
    ir_device_info_t robot;
    if (!resolve_device_info(IR_DEVICE_ROBOT_VACUUM, &robot)) return;
    esp_err_t r;
    r = ir_send_robot_key(&robot, IR_ROBOT_KEY_POWER); if (r == ESP_ERR_NO_MEM) { vTaskDelay(pdMS_TO_TICKS(50)); r = ir_send_robot_key(&robot, IR_ROBOT_KEY_POWER); } TEST_ASSERT_EQUAL(ESP_OK, r);
    r = ir_send_robot_key(&robot, IR_ROBOT_KEY_AUTO); if (r == ESP_ERR_NO_MEM) { vTaskDelay(pdMS_TO_TICKS(50)); r = ir_send_robot_key(&robot, IR_ROBOT_KEY_AUTO); } TEST_ASSERT_EQUAL(ESP_OK, r);
}

static void test_air_cleaner(void)
{
    ir_device_info_t air;
    if (!resolve_device_info(IR_DEVICE_AIR_PURIFIER, &air)) return;
    esp_err_t r;
    r = ir_send_air_cleaner_key(&air, IR_AIR_CLEANER_KEY_POWER); if (r == ESP_ERR_NO_MEM) { vTaskDelay(pdMS_TO_TICKS(50)); r = ir_send_air_cleaner_key(&air, IR_AIR_CLEANER_KEY_POWER); } TEST_ASSERT_EQUAL(ESP_OK, r);
    r = ir_send_air_cleaner_key(&air, IR_AIR_CLEANER_KEY_MODE); if (r == ESP_ERR_NO_MEM) { vTaskDelay(pdMS_TO_TICKS(50)); r = ir_send_air_cleaner_key(&air, IR_AIR_CLEANER_KEY_MODE); } TEST_ASSERT_EQUAL(ESP_OK, r);
}

static void test_dyson(void)
{
    ir_device_info_t dyson;
    if (!resolve_device_info(IR_DEVICE_DYSON, &dyson)) return;
    esp_err_t r;
    r = ir_send_dyson_key(&dyson, IR_DYSON_KEY_POWER); if (r == ESP_ERR_NO_MEM) { vTaskDelay(pdMS_TO_TICKS(50)); r = ir_send_dyson_key(&dyson, IR_DYSON_KEY_POWER); } TEST_ASSERT_EQUAL(ESP_OK, r);
    r = ir_send_dyson_key(&dyson, IR_DYSON_KEY_SWING); if (r == ESP_ERR_NO_MEM) { vTaskDelay(pdMS_TO_TICKS(50)); r = ir_send_dyson_key(&dyson, IR_DYSON_KEY_SWING); } TEST_ASSERT_EQUAL(ESP_OK, r);
}

static void test_camera(void)
{
    ir_device_info_t cam;
    if (!resolve_device_info(IR_DEVICE_CAMERA, &cam)) return;
    esp_err_t r;
    r = ir_send_camera_key(&cam, IR_CAMERA_KEY_POWER); if (r == ESP_ERR_NO_MEM) { vTaskDelay(pdMS_TO_TICKS(50)); r = ir_send_camera_key(&cam, IR_CAMERA_KEY_POWER); } TEST_ASSERT_EQUAL(ESP_OK, r);
    r = ir_send_camera_key(&cam, IR_CAMERA_KEY_SHOT); if (r == ESP_ERR_NO_MEM) { vTaskDelay(pdMS_TO_TICKS(50)); r = ir_send_camera_key(&cam, IR_CAMERA_KEY_SHOT); } TEST_ASSERT_EQUAL(ESP_OK, r);
}

static void test_heater(void)
{
    ir_device_info_t heater;
    if (!resolve_device_info(IR_DEVICE_HEATER, &heater)) return;
    esp_err_t r;
    r = ir_send_heater_key(&heater, IR_HEATER_KEY_POWER); if (r == ESP_ERR_NO_MEM) { vTaskDelay(pdMS_TO_TICKS(50)); r = ir_send_heater_key(&heater, IR_HEATER_KEY_POWER); } TEST_ASSERT_EQUAL(ESP_OK, r);
    r = ir_send_heater_key(&heater, IR_HEATER_KEY_TEMP_UP); if (r == ESP_ERR_NO_MEM) { vTaskDelay(pdMS_TO_TICKS(50)); r = ir_send_heater_key(&heater, IR_HEATER_KEY_TEMP_UP); } TEST_ASSERT_EQUAL(ESP_OK, r);
}

static void test_ac(void)
{
    ir_device_info_t ac;
    if (!resolve_device_info(IR_DEVICE_AC, &ac)) return;
    ir_ac_status_t st = {
        .power = IR_AC_POWER_ON,
        .mode = IR_AC_MODE_COOL,
        .temperature = 24,
        .wind_speed = IR_AC_WIND_AUTO,
        .swing = IR_AC_SWING_OFF,
    };
    esp_err_t r = ir_send_ac_key_command(&ac, IR_AC_KEY_POWER, &st, false);
    if (r == ESP_ERR_NO_MEM) { vTaskDelay(pdMS_TO_TICKS(50)); r = ir_send_ac_key_command(&ac, IR_AC_KEY_POWER, &st, false); }
    TEST_ASSERT_EQUAL(ESP_OK, r);
}

static void test_region_indexing(void)
{
    ir_province_t provinces[8]; size_t prov_cnt=0;
    if (irext_api_list_provinces(provinces, 8, &prov_cnt) != ESP_OK || prov_cnt == 0) return;
    ir_city_t cities[8]; size_t city_cnt=0;
    if (irext_api_list_cities(provinces[0].code, cities, 8, &city_cnt) != ESP_OK || city_cnt == 0) return;
    ir_operator_t ops[8]; size_t op_cnt=0;
    (void)irext_api_list_operators(cities[0].code, ops, 8, &op_cnt);
}

static void test_generic_key_and_low_level(void)
{
    ir_device_info_t tv;
    if (!resolve_device_info(IR_DEVICE_TV, &tv)) return;
    esp_err_t r;
    r = ir_send_key_command(&tv, (uint32_t)IR_TV_KEY_MUTE);
    if (r == ESP_ERR_NO_MEM) { vTaskDelay(pdMS_TO_TICKS(50)); r = ir_send_key_command(&tv, (uint32_t)IR_TV_KEY_MUTE); }
    TEST_ASSERT_EQUAL(ESP_OK, r);

    const uint32_t nec_lead[] = {9000, 4500};
    TEST_ASSERT_EQUAL(ESP_OK, ir_send_timing_us(nec_lead, 2));
}

static void test_listing_and_search(void)
{
    ir_category_t cats[16]; size_t cat_cnt=0;
    TEST_ASSERT_EQUAL(ESP_OK, ir_get_categories(cats, 16, &cat_cnt));
    // Search brands/models by a simple pattern
    char brands[20][IR_MAX_BRAND_NAME_LEN]; size_t bc=0;
    if (ir_get_supported_brands(IR_DEVICE_TV, brands, 20, &bc) == ESP_OK && bc>0) {
        size_t fc=0; // filtered
        TEST_ASSERT_EQUAL(ESP_OK, ir_search_brands(IR_DEVICE_TV, brands[0], brands, 20, &fc));
        char models[20][IR_MAX_MODEL_NAME_LEN]; size_t mc=0;
        if (ir_get_supported_models(IR_DEVICE_TV, brands[0], models, 20, &mc) == ESP_OK && mc>0) {
            size_t fmc=0;
            TEST_ASSERT_EQUAL(ESP_OK, ir_search_models(IR_DEVICE_TV, brands[0], models[0], models, 20, &fmc));
        }
    }
}

static void test_brand_id_and_find_device(void)
{
    char brands[5][IR_MAX_BRAND_NAME_LEN]; size_t bc=0;
    if (ir_get_supported_brands(IR_DEVICE_TV, brands, 5, &bc) == ESP_OK && bc>0) {
        uint32_t bid=0, mid=0;
        TEST_ASSERT_EQUAL(ESP_OK, ir_get_brand_id(IR_DEVICE_TV, brands[0], &bid));
        char models[5][IR_MAX_MODEL_NAME_LEN]; size_t mc=0;
        if (ir_get_supported_models(IR_DEVICE_TV, brands[0], models, 5, &mc) == ESP_OK && mc>0) {
            TEST_ASSERT_EQUAL(ESP_OK, ir_find_device_by_name(IR_DEVICE_TV, brands[0], models[0], &bid, &mid));
        }
    }
}

static void test_status_and_maintenance(void)
{
    TEST_ASSERT_TRUE(ir_is_initialized());
    TEST_ASSERT_EQUAL(ESP_OK, ir_refresh_auth_token());
    TEST_ASSERT_EQUAL(ESP_OK, ir_clear_cache());
    (void)ir_get_last_error();
}

static void ir_test_task(void *arg)
{
    // IR init
    ir_tx_config_t tx = {
        .tx_gpio = IR_TEST_TX_GPIO,
        .carrier_freq_hz = get_carrier_hz(),
        .resolution_hz = CONFIG_IR_RESOLUTION_HZ,
        .invert_signal = IR_TEST_INVERT_SIGNAL,
    };
    ir_irext_config_t api = {
        .server_url = CONFIG_IR_SERVER_URL,
        .app_key = CONFIG_IR_APP_KEY,
        .app_secret = CONFIG_IR_APP_SECRET,
        .auto_login = true,
        .cache_token = true,
        .timeout_ms = 15000,
    };
    ESP_ERROR_CHECK(ir_remote_init(&tx, &api));

    UNITY_BEGIN();
    RUN_TEST(test_tv);
    RUN_TEST(test_stb);
    RUN_TEST(test_box);
    RUN_TEST(test_iptv);
    RUN_TEST(test_dvd);
    RUN_TEST(test_fan);
    RUN_TEST(test_projector);
    RUN_TEST(test_stereo);
    RUN_TEST(test_bulb);
    RUN_TEST(test_robot);
    RUN_TEST(test_air_cleaner);
    RUN_TEST(test_dyson);
    RUN_TEST(test_camera);
    RUN_TEST(test_heater);
    RUN_TEST(test_ac);
    RUN_TEST(test_region_indexing);
    RUN_TEST(test_generic_key_and_low_level);
    RUN_TEST(test_listing_and_search);
    RUN_TEST(test_brand_id_and_find_device);
    RUN_TEST(test_status_and_maintenance);
    UNITY_END();

    ESP_LOGI(TAG, "All tests finished");
    vTaskDelete(NULL);
}

void app_main(void)
{
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << 7,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    ESP_ERROR_CHECK(gpio_set_level(7, 1));

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(example_connect());
    
    // Run tests in a dedicated task with larger stack to avoid printf/Unity deep stack usage
    const uint32_t stack_size = 12288; // bytes
    xTaskCreate(ir_test_task, "ir_test_task", stack_size, NULL, 5, NULL);
} 