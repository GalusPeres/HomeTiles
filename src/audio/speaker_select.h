#pragma once

// Maps the active device to its speaker board file (speaker_board.h). Only
// speaker files include this header, so adding a speaker device never
// rebuilds the rest of the firmware.
//
// The ESP32-P4 panels have a speaker codec and a microphone: speaker,
// Assist capture and the on-panel wake word (microWakeWord on TensorFlow
// Lite Micro) run on them through the shared core in src/audio/.
// A board file only names its chips, addresses and pins (contract in
// src/audio/audio_board.h). The ESP32-S3 panels have no voice support.

#include "src/devices/device_select.h"

// Waveshare ESP32-P4-WIFI6-Touch-LCD boards (Waveshare BSPs): ES8311 codec,
// NS4150B amplifier on GPIO53, ES7210 microphone ADC, the same pins on all.
#if defined(DEVICE_WAVESHARE_TOUCH_LCD_7B)
#define HOMETILES_SPEAKER_BOARD "src/devices/waveshare_touch_lcd_7b/speaker_board.h"
#elif defined(DEVICE_WAVESHARE_TOUCH_LCD_8)
#define HOMETILES_SPEAKER_BOARD "src/devices/waveshare_touch_lcd_8/speaker_board.h"
#elif defined(DEVICE_WAVESHARE_TOUCH_LCD_7)
#define HOMETILES_SPEAKER_BOARD "src/devices/waveshare_touch_lcd_7/speaker_board.h"
#elif defined(DEVICE_WAVESHARE_TOUCH_LCD_10_1)
#define HOMETILES_SPEAKER_BOARD "src/devices/waveshare_touch_lcd_10_1/speaker_board.h"
#elif defined(DEVICE_WAVESHARE_TOUCH_LCD_4_3)
#define HOMETILES_SPEAKER_BOARD "src/devices/waveshare_touch_lcd_4_3/speaker_board.h"
#elif defined(DEVICE_WAVESHARE_TOUCH_LCD_5)
#define HOMETILES_SPEAKER_BOARD "src/devices/waveshare_touch_lcd_5/speaker_board.h"
#elif defined(DEVICE_WAVESHARE_4B)
#define HOMETILES_SPEAKER_BOARD "src/devices/waveshare_4b/speaker_board.h"
// Guition boards (Guition schematics): one ES8311 for speaker and microphone,
// NS4150 amplifier. The original-panel JC1060P470C stays out: its board
// identity is unproven (like its camera, see device_select.h).
#elif defined(DEVICE_GUITION_JC8012P4A1)
#define HOMETILES_SPEAKER_BOARD "src/devices/guition_jc8012p4a1/speaker_board.h"
#elif defined(DEVICE_GUITION_JC8012P4A1_V2)
#define HOMETILES_SPEAKER_BOARD "src/devices/guition_jc8012p4a1_v2/speaker_board.h"
#elif defined(DEVICE_GUITION_JC1060P470C_V2)
#define HOMETILES_SPEAKER_BOARD "src/devices/guition_jc1060p470c_v2/speaker_board.h"
#elif defined(DEVICE_GUITION_JC4880P443_PORTRAIT)
#define HOMETILES_SPEAKER_BOARD "src/devices/guition_jc4880p443_portrait/speaker_board.h"
// M5Stack Tab5 (Espressif BSP, M5Unified): ES8388 codec, ES7210 microphone
// ADC; the codecs and the amplifier enable sit behind M5Unified's I2C.
#elif defined(DEVICE_M5STACKS_TAB5)
#define HOMETILES_SPEAKER_BOARD "src/devices/m5stacks_tab5/speaker_board.h"
#define HOMETILES_CODEC_BUS_BY_BOARD 1
#define HOMETILES_SPEAKER_AMP_BY_BOARD 1
#endif

#if defined(HOMETILES_SPEAKER_BOARD)
#define HOMETILES_SPEAKER 1
#define HOMETILES_WAKE_WORD 1
#endif
