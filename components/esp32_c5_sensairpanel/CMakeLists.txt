idf_component_register(
    SRCS "esp32_c5_sensairpanel.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "priv_include"
    PRIV_REQUIRES
        "esp_lcd"
    REQUIRES
        "driver"
        "spiffs"
)

idf_component_set_property(${COMPONENT_NAME} WHOLE_ARCHIVE TRUE)