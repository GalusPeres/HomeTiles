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
#define FW_VERSION "v0.6.12b50"
#endif

// Camera beta builds of every camera board (HOMETILES_LOCAL_CAMERA in
// device_select.h) carry the beta number.
#if defined(HOMETILES_CAMERA_BETA)
#undef FW_VERSION
#define FW_VERSION "v0.6.12b50"
#endif

// Local test builds of the other profiles (for example Waveshare 4B and
// Guition ESP32-S3) carry the same beta number as the camera builds.
#if defined(HOMETILES_TEST_BETA) && !defined(HOMETILES_CAMERA_BETA) && \
    !defined(HOMETILES_ISSUE38_BETA)
#undef FW_VERSION
#define FW_VERSION "v0.6.12b50"
#endif

#ifndef FW_VERSION
#error "FW_VERSION is missing in version.txt"
#endif
