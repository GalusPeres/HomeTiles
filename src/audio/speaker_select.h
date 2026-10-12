#pragma once

// Maps the active device to its speaker board file (speaker_board.h). Only
// speaker files include this header, so adding a speaker device never
// rebuilds the rest of the firmware.

#include "src/devices/device_select.h"

// Waveshare ESP32-P4-WIFI6-Touch-LCD-7B: ES8311 codec and NS4150B amplifier
// on the PH 2.0 speaker connector, ES7210 microphone ADC for Assist capture
// and the on-panel wake word (microWakeWord on TensorFlow Lite Micro).
#if defined(DEVICE_WAVESHARE_TOUCH_LCD_7B)
#define HOMETILES_SPEAKER 1
#define HOMETILES_SPEAKER_BOARD "src/devices/waveshare_touch_lcd_7b/speaker_board.h"
#define HOMETILES_WAKE_WORD 1
#endif
