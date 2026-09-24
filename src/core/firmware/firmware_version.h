#pragma once

#include "version.txt"

#if defined(DEVICE_GUITION_JC8012P4A1) && \
    defined(HOMETILES_ISSUE30_SAFE_BETA)
#undef FW_VERSION
#define FW_VERSION "v0.6.8b2"
#endif

#if defined(DEVICE_GUITION_JC8012P4A1_V2) && \
    defined(HOMETILES_ISSUE38_BETA)
#undef FW_VERSION
#define FW_VERSION "v0.6.12b31"
#endif

#if defined(DEVICE_WAVESHARE_TOUCH_LCD_8) && \
    defined(HOMETILES_CAMERA_BETA)
#undef FW_VERSION
#define FW_VERSION "v0.6.12b31"
#endif
#if defined(DEVICE_M5STACKS_TAB5) && \
    defined(HOMETILES_CAMERA_BETA)
#undef FW_VERSION
#define FW_VERSION "v0.6.12b31"
#endif

#ifndef FW_VERSION
#error "FW_VERSION is missing in version.txt"
#endif
