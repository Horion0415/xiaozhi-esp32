SET(SOURCES screens/ui_ScreenLight.c
    screens/ui_ScreenAirCon.c
    screens/ui_ScreenMusic.c
    screens/ui_ScreenKeyBoard.c
    ui.c
    components/ui_comp_hook.c
    ui_helpers.c
    images/ui_img_back_png.c
    images/ui_img_cloud_back_png.c
    images/ui_img_music_icon_png.c
    fonts/ui_font_FontKaiTi22.c
    fonts/ui_font_FontKaiTi30.c
    fonts/ui_font_FontQingniao30.c)

add_library(ui ${SOURCES})
target_include_directories(ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
