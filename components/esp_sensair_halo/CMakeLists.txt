idf_component_register(
    SRCS "esp_sensair_halo.c"
         "esp_sensair_halo_lvgl.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "priv_include"
    REQUIRES driver esp_lcd esp_lcd_touch esp_lvgl_adapter
            espressif__esp_codec_dev espressif__led_strip espressif__touch_ic_bs8112a3
            espressif__i2c_bus espressif2022__bmi270 tangyumei3535__bmm150_sensorapi
    PRIV_REQUIRES fatfs esp_timer esp_video espressif__adc_mic
)
