#pragma once

// Central device selection for the shared project.
// Arduino IDE builds usually do not provide per-target build flags.
// For a quick manual switch, uncomment exactly one target below:
//
#if !defined(HOMETILES_CI_TARGET)
#define DEVICE_WAVESHARE_4B
// #define DEVICE_WAVESHARE_TOUCH_LCD_4_3
// #define DEVICE_WAVESHARE_TOUCH_LCD_7
// #define DEVICE_WAVESHARE_TOUCH_LCD_7B
// #define DEVICE_WAVESHARE_TOUCH_LCD_8
// #define DEVICE_WAVESHARE_TOUCH_LCD_10_1
// #define DEVICE_WAVESHARE_S3_TOUCH_LCD_4
// #define DEVICE_WAVESHARE_S3_TOUCH_LCD_4B
// #define DEVICE_LAYOUT_TEST_1024X600
// #define DEVICE_LAYOUT_TEST_480X480
// #define DEVICE_M5STACKS_TAB5
// #define DEVICE_GUITION_JC8012P4A1
// #define DEVICE_GUITION_JC8012P4A1_V2
// #define DEVICE_GUITION_JC1060P470C
// #define DEVICE_GUITION_JC1060P470C_V2
// #define DEVICE_GUITION_JC4880P443_PORTRAIT
// #define DEVICE_GUITION_ESP32_4848S040
#endif
//
// If nothing is selected, the project defaults to Waveshare 4B.

#if defined(DEVICE_TAB5) && !defined(DEVICE_M5STACKS_TAB5)
#define DEVICE_M5STACKS_TAB5
#endif

#if defined(DEVICE_WAVESHARE_WIFI6_TOUCH_LCD_8) && !defined(DEVICE_WAVESHARE_TOUCH_LCD_8)
#define DEVICE_WAVESHARE_TOUCH_LCD_8
#endif

#if (defined(DEVICE_WAVESHARE_4B) + \
     defined(DEVICE_WAVESHARE_TOUCH_LCD_4_3) + \
     defined(DEVICE_WAVESHARE_TOUCH_LCD_7) + \
     defined(DEVICE_WAVESHARE_TOUCH_LCD_7B) + \
     defined(DEVICE_WAVESHARE_TOUCH_LCD_8) + \
     defined(DEVICE_WAVESHARE_TOUCH_LCD_10_1) + \
     defined(DEVICE_WAVESHARE_S3_TOUCH_LCD_4) + \
     defined(DEVICE_WAVESHARE_S3_TOUCH_LCD_4B) + \
     defined(DEVICE_LAYOUT_TEST_1024X600) + \
     defined(DEVICE_LAYOUT_TEST_480X480) + \
     defined(DEVICE_M5STACKS_TAB5) + \
     defined(DEVICE_GUITION_JC8012P4A1) + \
     defined(DEVICE_GUITION_JC8012P4A1_V2) + \
     defined(DEVICE_GUITION_JC1060P470C) + \
     defined(DEVICE_GUITION_JC1060P470C_V2) + \
     defined(DEVICE_GUITION_JC4880P443_PORTRAIT) + \
     defined(DEVICE_GUITION_ESP32_4848S040)) > 1
#error "Select only one device target."
#endif

#if !defined(DEVICE_WAVESHARE_4B) && \
    !defined(DEVICE_WAVESHARE_TOUCH_LCD_4_3) && \
    !defined(DEVICE_WAVESHARE_TOUCH_LCD_7) && \
    !defined(DEVICE_WAVESHARE_TOUCH_LCD_7B) && \
    !defined(DEVICE_WAVESHARE_TOUCH_LCD_8) && \
    !defined(DEVICE_WAVESHARE_TOUCH_LCD_10_1) && \
    !defined(DEVICE_WAVESHARE_S3_TOUCH_LCD_4) && \
    !defined(DEVICE_WAVESHARE_S3_TOUCH_LCD_4B) && \
    !defined(DEVICE_LAYOUT_TEST_1024X600) && \
    !defined(DEVICE_LAYOUT_TEST_480X480) && \
    !defined(DEVICE_M5STACKS_TAB5) && \
    !defined(DEVICE_GUITION_JC8012P4A1) && \
    !defined(DEVICE_GUITION_JC8012P4A1_V2) && \
    !defined(DEVICE_GUITION_JC1060P470C) && \
    !defined(DEVICE_GUITION_JC1060P470C_V2) && \
    !defined(DEVICE_GUITION_JC4880P443_PORTRAIT) && \
    !defined(DEVICE_GUITION_ESP32_4848S040) && \
    defined(HOMETILES_CI_TARGET)
#error "HOMETILES_CI_TARGET requires one DEVICE_* build flag."
#endif

#if !defined(DEVICE_WAVESHARE_4B) && \
    !defined(DEVICE_WAVESHARE_TOUCH_LCD_4_3) && \
    !defined(DEVICE_WAVESHARE_TOUCH_LCD_7) && \
    !defined(DEVICE_WAVESHARE_TOUCH_LCD_7B) && \
    !defined(DEVICE_WAVESHARE_TOUCH_LCD_8) && \
    !defined(DEVICE_WAVESHARE_TOUCH_LCD_10_1) && \
    !defined(DEVICE_WAVESHARE_S3_TOUCH_LCD_4) && \
    !defined(DEVICE_WAVESHARE_S3_TOUCH_LCD_4B) && \
    !defined(DEVICE_LAYOUT_TEST_1024X600) && \
    !defined(DEVICE_LAYOUT_TEST_480X480) && \
    !defined(DEVICE_M5STACKS_TAB5) && \
    !defined(DEVICE_GUITION_JC8012P4A1) && \
    !defined(DEVICE_GUITION_JC8012P4A1_V2) && \
    !defined(DEVICE_GUITION_JC1060P470C) && \
    !defined(DEVICE_GUITION_JC1060P470C_V2) && \
    !defined(DEVICE_GUITION_JC4880P443_PORTRAIT) && \
    !defined(DEVICE_GUITION_ESP32_4848S040) && \
    !defined(HOMETILES_CI_TARGET)
#define DEVICE_WAVESHARE_4B
#endif

// Both JC8012 revisions share the board-level UI and lifecycle behavior, but
// retain separate device drivers, display tables and OTA identities.
#if defined(DEVICE_GUITION_JC8012P4A1) || \
    defined(DEVICE_GUITION_JC8012P4A1_V2)
#define DEVICE_GUITION_JC8012P4A1_FAMILY
#endif

// These Waveshare P4 products share the same I2C/touch, backlight, SDMMC and
// ESP-Hosted pin contracts. Each exact panel keeps its own controller, timing,
// layout and device driver.
#if defined(DEVICE_WAVESHARE_TOUCH_LCD_4_3) || \
    defined(DEVICE_WAVESHARE_TOUCH_LCD_7) || \
    defined(DEVICE_WAVESHARE_TOUCH_LCD_7B) || \
    defined(DEVICE_WAVESHARE_TOUCH_LCD_8) || \
    defined(DEVICE_WAVESHARE_TOUCH_LCD_10_1) || \
    defined(DEVICE_LAYOUT_TEST_1024X600) || \
    defined(DEVICE_LAYOUT_TEST_480X480)
#define DEVICE_WAVESHARE_TOUCH_LCD_X
#endif

// Both JC1060 panel revisions share board-level storage, touch, backlight and
// lifecycle behavior. Their panel reset pin, timing and init table stay in
// separate exact device drivers.
#if defined(DEVICE_GUITION_JC1060P470C) || \
    defined(DEVICE_GUITION_JC1060P470C_V2)
#define DEVICE_GUITION_JC1060P470C_FAMILY
#endif

// Built-in camera. The exact JC8012P4A1 V2 profile carries the demo-verified
// OV02C10 SCCB/CSI wiring; the Waveshare 8-inch ESP32-P4-WIFI6-Touch-LCD
// front camera (OV5647) and the M5Stack Tab5 camera (SC202CS/SC2356) are
// enabled only in camera beta builds (HOMETILES_CAMERA_BETA) until hardware
// validation. V1 and every other board keep the capture path out of their images.
#if defined(DEVICE_GUITION_JC8012P4A1_V2) || \
    (defined(DEVICE_WAVESHARE_TOUCH_LCD_8) && defined(HOMETILES_CAMERA_BETA)) || \
    (defined(DEVICE_M5STACKS_TAB5) && defined(HOMETILES_CAMERA_BETA))
#define HOMETILES_LOCAL_CAMERA 1
#endif

// Native ESP-IDF MIPI-DSI profiles share the same two-framebuffer camera
// presenter. M5GFX Tab5 and Arduino_GFX B4 intentionally keep their library
// owned display backends.
#if defined(DEVICE_WAVESHARE_TOUCH_LCD_4_3) || \
    defined(DEVICE_WAVESHARE_TOUCH_LCD_7) || \
    defined(DEVICE_WAVESHARE_TOUCH_LCD_7B) || \
    defined(DEVICE_WAVESHARE_TOUCH_LCD_8) || \
    defined(DEVICE_WAVESHARE_TOUCH_LCD_10_1) || \
    defined(DEVICE_GUITION_JC8012P4A1) || \
    defined(DEVICE_GUITION_JC8012P4A1_V2) || \
    defined(DEVICE_GUITION_JC1060P470C) || \
    defined(DEVICE_GUITION_JC1060P470C_V2) || \
    defined(DEVICE_GUITION_JC4880P443_PORTRAIT)
#define DEVICE_P4_IDF_DSI
#endif

// The 480x480 ESP32-S3 RGB boards share only proven S3 framebuffer,
// storage and OTA lifecycle handling. Panel wiring and init remain separate.
#if defined(DEVICE_GUITION_ESP32_4848S040) || \
    defined(DEVICE_WAVESHARE_S3_TOUCH_LCD_4) || \
    defined(DEVICE_WAVESHARE_S3_TOUCH_LCD_4B)
#define DEVICE_ESP32_S3_RGB_480
#endif

#if defined(DEVICE_LAYOUT_TEST_1024X600) || \
    defined(DEVICE_WAVESHARE_TOUCH_LCD_7B) || \
    defined(DEVICE_GUITION_JC1060P470C) || \
    defined(DEVICE_GUITION_JC1060P470C_V2)
#define DEVICE_LAYOUT_1024X600
#endif

#if defined(DEVICE_LAYOUT_TEST_480X480) || \
    defined(DEVICE_WAVESHARE_TOUCH_LCD_4_3) || \
    defined(DEVICE_GUITION_JC4880P443_PORTRAIT) || \
    defined(DEVICE_GUITION_ESP32_4848S040) || \
    defined(DEVICE_WAVESHARE_S3_TOUCH_LCD_4) || \
    defined(DEVICE_WAVESHARE_S3_TOUCH_LCD_4B)
#define DEVICE_LAYOUT_480X480
#endif

// The 8" and 10.1" panels share the 1280x800 HomeTiles layout.
#if defined(DEVICE_WAVESHARE_TOUCH_LCD_8) || \
    defined(DEVICE_WAVESHARE_TOUCH_LCD_10_1)
#define DEVICE_WAVESHARE_TOUCH_LCD_1280X800
#endif
