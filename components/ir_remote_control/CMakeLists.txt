# IRext Online IR Remote Control Component
cmake_minimum_required(VERSION 3.16)

# Component source files
set(IR_SRCS
    # Core API layer
    "src/ir_remote_control.c"

    # IRext Web API integration
    "src/irext_api.c"
    "src/irext_auth.c"

    # Hardware abstraction layer
    "src/ir_transmitter.c"
)

# Include directories
set(IR_INCLUDES
    "include"
    "src/include"
)

# Component dependencies (keep minimal and explicit)
set(IR_REQUIRES
    "esp_http_client"
    "driver"
    "nvs_flash"
    "esp_timer"
    "esp_driver_ledc"
    "json"
    "esp_driver_gptimer"
)

# Register component
idf_component_register(
    SRCS ${IR_SRCS}
    INCLUDE_DIRS ${IR_INCLUDES}
    REQUIRES ${IR_REQUIRES}
)

# Optional debug flag
if(CONFIG_IR_ENABLE_DEBUG_LOGS)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE IR_DEBUG_ENABLED=1)
endif() 