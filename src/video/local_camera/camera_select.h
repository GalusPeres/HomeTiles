#pragma once

// Maps the active device to its built-in camera drivers (see camera_driver.h).
// Only camera files include this header, so adding a camera device never
// rebuilds the rest of the firmware.

#include "src/devices/device_select.h"

#if defined(HOMETILES_LOCAL_CAMERA)

#if defined(DEVICE_GUITION_JC8012P4A1_V2)
#define HOMETILES_CAMERA_SENSOR_OV02C10 1
#define HOMETILES_LOCAL_CAMERA_BOARD "src/devices/guition_jc8012p4a1_v2/local_camera_board.h"
#endif

#if defined(DEVICE_WAVESHARE_TOUCH_LCD_8)
#define HOMETILES_CAMERA_SENSOR_OV5647 1
#define HOMETILES_LOCAL_CAMERA_BOARD "src/devices/waveshare_touch_lcd_8/local_camera_board.h"
#endif

#if defined(DEVICE_M5STACKS_TAB5)
#define HOMETILES_CAMERA_SENSOR_SC202CS 1
#define HOMETILES_LOCAL_CAMERA_BOARD "src/devices/m5stacks_tab5/local_camera_board.h"
#endif

#if !defined(HOMETILES_LOCAL_CAMERA_BOARD)
#error "HOMETILES_LOCAL_CAMERA is set but no camera board file is selected"
#endif

#endif  // defined(HOMETILES_LOCAL_CAMERA)
