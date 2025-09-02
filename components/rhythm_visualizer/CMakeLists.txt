cmake_minimum_required(VERSION 3.16)

set(RHYTHM_SRCS
    "src/rhythm_visualizer.c"
    "src/spectrum_analyzer.c"
    "src/color_mapper.c"
)

set(RHYTHM_INCLUDES
    "include"
)

set(RHYTHM_REQUIRES
    "esp_timer"
    "freertos"
    "esp_common"
    "log"
)

idf_component_register(
    SRCS ${RHYTHM_SRCS}
    INCLUDE_DIRS ${RHYTHM_INCLUDES}
    REQUIRES ${RHYTHM_REQUIRES}
) 