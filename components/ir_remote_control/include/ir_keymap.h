/* SPDX-FileCopyrightText: 2025
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Keycodes aligned with IRext "按键映射" table.
 * Values are fixed to the table indices (0..13). For TV/STB digits, use 14..23.
 */

typedef enum {
    IR_AC_KEY_POWER = 0,
    IR_AC_KEY_MODE = 1,
    IR_AC_KEY_TEMP_UP = 2,
    IR_AC_KEY_TEMP_DOWN = 3,
    IR_AC_KEY_TEMP_UP_ALT = 7,
    IR_AC_KEY_TEMP_DOWN_ALT = 8,
    IR_AC_KEY_WIND_SPEED = 9,
    IR_AC_KEY_SWING = 10,
    IR_AC_KEY_WIND_FIX = 11,
} ir_ac_keycode_t;

/* TV: 0..13 per table, 14..23 are digits 0..9 */
typedef enum {
    IR_TV_KEY_POWER = 0,
    IR_TV_KEY_MUTE = 1,
    IR_TV_KEY_UP = 2,
    IR_TV_KEY_DOWN = 3,
    IR_TV_KEY_LEFT = 4,
    IR_TV_KEY_RIGHT = 5,
    IR_TV_KEY_OK = 6,
    IR_TV_KEY_VOL_UP = 7,
    IR_TV_KEY_VOL_DOWN = 8,
    IR_TV_KEY_BACK = 9,
    IR_TV_KEY_INPUT = 10,
    IR_TV_KEY_MENU = 11,
    IR_TV_KEY_HOME = 12,
    IR_TV_KEY_SET = 13,
    IR_TV_KEY_DIGIT_0 = 14,
    IR_TV_KEY_DIGIT_1 = 15,
    IR_TV_KEY_DIGIT_2 = 16,
    IR_TV_KEY_DIGIT_3 = 17,
    IR_TV_KEY_DIGIT_4 = 18,
    IR_TV_KEY_DIGIT_5 = 19,
    IR_TV_KEY_DIGIT_6 = 20,
    IR_TV_KEY_DIGIT_7 = 21,
    IR_TV_KEY_DIGIT_8 = 22,
    IR_TV_KEY_DIGIT_9 = 23,
} ir_tv_keycode_t;

/* STB/IPTV share the same basic mapping; 14..23 are digits */
typedef enum {
    IR_STB_KEY_POWER = 0,
    IR_STB_KEY_MUTE = 1,
    IR_STB_KEY_UP = 2,
    IR_STB_KEY_DOWN = 3,
    IR_STB_KEY_LEFT = 4,
    IR_STB_KEY_RIGHT = 5,
    IR_STB_KEY_OK = 6,
    IR_STB_KEY_VOL_UP = 7,
    IR_STB_KEY_VOL_DOWN = 8,
    IR_STB_KEY_BACK = 9,
    IR_STB_KEY_INPUT = 10,
    IR_STB_KEY_MENU = 11,
    IR_STB_KEY_PREV = 12,
    IR_STB_KEY_NEXT = 13,
    IR_STB_KEY_DIGIT_0 = 14,
    IR_STB_KEY_DIGIT_1 = 15,
    IR_STB_KEY_DIGIT_2 = 16,
    IR_STB_KEY_DIGIT_3 = 17,
    IR_STB_KEY_DIGIT_4 = 18,
    IR_STB_KEY_DIGIT_5 = 19,
    IR_STB_KEY_DIGIT_6 = 20,
    IR_STB_KEY_DIGIT_7 = 21,
    IR_STB_KEY_DIGIT_8 = 22,
    IR_STB_KEY_DIGIT_9 = 23,
} ir_stb_keycode_t;

typedef enum {
    IR_BOX_KEY_POWER = 0,
    IR_BOX_KEY_UP = 1,
    IR_BOX_KEY_DOWN = 2,
    IR_BOX_KEY_LEFT = 3,
    IR_BOX_KEY_RIGHT = 4,
    IR_BOX_KEY_OK = 5,
    IR_BOX_KEY_VOL_UP = 6,
    IR_BOX_KEY_VOL_DOWN = 7,
    IR_BOX_KEY_BACK = 8,
    IR_BOX_KEY_MENU = 9,
    IR_BOX_KEY_HOME = 10,
} ir_box_keycode_t;

typedef enum {
    IR_IPTV_KEY_POWER = 0,
    IR_IPTV_KEY_MUTE = 1,
    IR_IPTV_KEY_UP = 2,
    IR_IPTV_KEY_DOWN = 3,
    IR_IPTV_KEY_LEFT = 4,
    IR_IPTV_KEY_RIGHT = 5,
    IR_IPTV_KEY_OK = 6,
    IR_IPTV_KEY_VOL_UP = 7,
    IR_IPTV_KEY_VOL_DOWN = 8,
    IR_IPTV_KEY_BACK = 9,
    IR_IPTV_KEY_INPUT = 10,
    IR_IPTV_KEY_MENU = 11,
    IR_IPTV_KEY_PREV = 12,
    IR_IPTV_KEY_NEXT = 13,
} ir_iptv_keycode_t;

typedef enum {
    IR_DVD_KEY_POWER = 0,
    IR_DVD_KEY_UP = 1,
    IR_DVD_KEY_DOWN = 2,
    IR_DVD_KEY_LEFT = 3,
    IR_DVD_KEY_RIGHT = 4,
    IR_DVD_KEY_OK = 5,
    IR_DVD_KEY_VOL_UP = 6,
    IR_DVD_KEY_VOL_DOWN = 7,
    IR_DVD_KEY_PLAY = 8,
    IR_DVD_KEY_PAUSE = 9,
    IR_DVD_KEY_EJECT = 10,
    IR_DVD_KEY_REWIND = 11,
    IR_DVD_KEY_FAST_FORWARD = 12,
    IR_DVD_KEY_MENU = 13,
} ir_dvd_keycode_t;

typedef enum {
    IR_FAN_KEY_POWER = 0,
    IR_FAN_KEY_UP = 1,
    IR_FAN_KEY_DOWN = 2,
    IR_FAN_KEY_LEFT = 3,
    IR_FAN_KEY_RIGHT = 4,
    IR_FAN_KEY_OK = 5,
    IR_FAN_KEY_WIND_UP = 6,
    IR_FAN_KEY_WIND_DOWN = 7,
    IR_FAN_KEY_SWING = 8,
    IR_FAN_KEY_WIND_SPEED = 9,
    IR_FAN_KEY_WIND_TYPE = 10,
    IR_FAN_KEY_BACK = 11,
    IR_FAN_KEY_HOME = 12,
    IR_FAN_KEY_MENU = 13,
} ir_fan_keycode_t;

typedef enum {
    IR_PROJECTOR_KEY_POWER = 0,
    IR_PROJECTOR_KEY_UP = 1,
    IR_PROJECTOR_KEY_DOWN = 2,
    IR_PROJECTOR_KEY_LEFT = 3,
    IR_PROJECTOR_KEY_RIGHT = 4,
    IR_PROJECTOR_KEY_OK = 5,
    IR_PROJECTOR_KEY_VOL_UP = 6,
    IR_PROJECTOR_KEY_VOL_DOWN = 7,
    IR_PROJECTOR_KEY_ZOOM_OUT = 8,
    IR_PROJECTOR_KEY_MENU = 9,
    IR_PROJECTOR_KEY_ZOOM_IN = 10,
    IR_PROJECTOR_KEY_BACK = 11,
    IR_PROJECTOR_KEY_HOME = 12,
} ir_projector_keycode_t;

typedef enum {
    IR_STEREO_KEY_POWER = 0,
    IR_STEREO_KEY_UP = 1,
    IR_STEREO_KEY_DOWN = 2,
    IR_STEREO_KEY_LEFT = 3,
    IR_STEREO_KEY_RIGHT = 4,
    IR_STEREO_KEY_OK = 5,
    IR_STEREO_KEY_VOL_UP = 6,
    IR_STEREO_KEY_VOL_DOWN = 7,
    IR_STEREO_KEY_MUTE = 8,
    IR_STEREO_KEY_MENU = 9,
    IR_STEREO_KEY_POWER_ALT = 10,
    IR_STEREO_KEY_BACK = 11,
    IR_STEREO_KEY_HOME = 12,
} ir_stereo_keycode_t;

typedef enum {
    IR_BULB_KEY_POWER = 0,
    IR_BULB_KEY_COLOR_1 = 1,
    IR_BULB_KEY_COLOR_2 = 2,
    IR_BULB_KEY_COLOR_3 = 3,
    IR_BULB_KEY_COLOR_4 = 4,
    IR_BULB_KEY_COLOR_0 = 5,
    IR_BULB_KEY_BRIGHT_UP = 6,
    IR_BULB_KEY_BRIGHT_DOWN = 7,
    IR_BULB_KEY_ON = 8,
    IR_BULB_KEY_COLOR_FLOW = 9,
    IR_BULB_KEY_OFF = 10,
    IR_BULB_KEY_BACK = 11,
    IR_BULB_KEY_HOME = 12,
    IR_BULB_KEY_MENU = 13,
} ir_bulb_keycode_t;

typedef enum {
    IR_ROBOT_KEY_POWER = 0,
    IR_ROBOT_KEY_FORWARD = 1,
    IR_ROBOT_KEY_BACKWARD = 2,
    IR_ROBOT_KEY_LEFT = 3,
    IR_ROBOT_KEY_RIGHT = 4,
    IR_ROBOT_KEY_START_STOP = 5,
    IR_ROBOT_KEY_PLUS = 6,
    IR_ROBOT_KEY_MINUS = 7,
    IR_ROBOT_KEY_AUTO = 8,
    IR_ROBOT_KEY_SPOT = 9,
    IR_ROBOT_KEY_SPEED = 10,
    IR_ROBOT_KEY_TIMING = 11,
    IR_ROBOT_KEY_CHARGE = 12,
    IR_ROBOT_KEY_PRESERVE = 13,
} ir_robot_keycode_t;

typedef enum {
    IR_AIR_CLEANER_KEY_POWER = 0,
    IR_AIR_CLEANER_KEY_UP = 1,
    IR_AIR_CLEANER_KEY_DOWN = 2,
    IR_AIR_CLEANER_KEY_LEFT = 3,
    IR_AIR_CLEANER_KEY_RIGHT = 4,
    IR_AIR_CLEANER_KEY_ION = 5,
    IR_AIR_CLEANER_KEY_PLUS = 6,
    IR_AIR_CLEANER_KEY_MINUS = 7,
    IR_AIR_CLEANER_KEY_AUTO = 8,
    IR_AIR_CLEANER_KEY_WIND_SPEED = 9,
    IR_AIR_CLEANER_KEY_MODE = 10,
    IR_AIR_CLEANER_KEY_TIMING = 11,
    IR_AIR_CLEANER_KEY_LIGHT = 12,
    IR_AIR_CLEANER_KEY_FORCE = 13,
} ir_air_cleaner_keycode_t;

typedef enum {
    IR_DYSON_KEY_POWER = 0,
    IR_DYSON_KEY_WIND_UP = 1,
    IR_DYSON_KEY_WIND_DOWN = 2,
    IR_DYSON_KEY_TIME_DOWN = 3,
    IR_DYSON_KEY_TIME_UP = 4,
    IR_DYSON_KEY_AUTO = 5,
    IR_DYSON_KEY_TEMP_UP = 6,
    IR_DYSON_KEY_TEMP_DOWN = 7,
    IR_DYSON_KEY_SWING = 8,
    IR_DYSON_KEY_DIFFUSE = 9,
    IR_DYSON_KEY_PERSONAL = 10,
    IR_DYSON_KEY_TIMING = 11,
    IR_DYSON_KEY_SLEEP = 12,
    IR_DYSON_KEY_COOL = 13,
} ir_dyson_keycode_t;

typedef enum {
    IR_CAMERA_KEY_POWER = 0,
    IR_CAMERA_KEY_UP = 1,
    IR_CAMERA_KEY_DOWN = 2,
    IR_CAMERA_KEY_LEFT = 3,
    IR_CAMERA_KEY_RIGHT = 4,
    IR_CAMERA_KEY_SHOT = 5,
    IR_CAMERA_KEY_FOCAL_PLUS = 6,
    IR_CAMERA_KEY_FOCAL_MINUS = 7,
    IR_CAMERA_KEY_CAMERA = 8,
    IR_CAMERA_KEY_VIDEO = 9,
    IR_CAMERA_KEY_TIMING = 10,
    IR_CAMERA_KEY_FLASHING = 11,
    IR_CAMERA_KEY_MICRO = 12,
    IR_CAMERA_KEY_NIGHT = 13,
} ir_camera_keycode_t;

typedef enum {
    IR_HEATER_KEY_POWER = 0,
    IR_HEATER_KEY_TEMP_UP = 1,
    IR_HEATER_KEY_TEMP_DOWN = 2,
    IR_HEATER_KEY_TIME_DOWN = 3,
    IR_HEATER_KEY_TIME_UP = 4,
    IR_HEATER_KEY_AUTO = 5,
    IR_HEATER_KEY_CAPACITY_UP = 6,
    IR_HEATER_KEY_CAPACITY_DOWN = 7,
    IR_HEATER_KEY_MAX = 8,
    IR_HEATER_KEY_KEEP_WARM = 9,
    IR_HEATER_KEY_TIMING = 10,
    IR_HEATER_KEY_SAVE = 11,
    IR_HEATER_KEY_CHANGE_FREQ = 12,
    IR_HEATER_KEY_DISPLAY = 13,
} ir_heater_keycode_t;

/* Helpers for TV/STB digits */
#define IR_TV_KEY_DIGIT(n)   ((uint32_t)(14 + ((n) % 10)))
#define IR_STB_KEY_DIGIT(n)  ((uint32_t)(14 + ((n) % 10)))

#ifdef __cplusplus
}
#endif 