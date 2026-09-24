#pragma once

#include <FS.h>

#include "src/devices/device_select.h"
#include "src/devices/device_types.h"
#include "src/devices/m5stacks_tab5/hardware_io_profile.h"

namespace DeviceM5StacksTab5 {

// Built-in SC202CS (SC2356) camera, camera beta builds only (device_select.h).
#if defined(HOMETILES_LOCAL_CAMERA)
inline constexpr bool kBuiltinCamera = true;
#else
inline constexpr bool kBuiltinCamera = false;
#endif

inline constexpr Device::Profile kProfile{
    "m5stacks_tab5",
    "M5Stacks Tab5",
    1280,
    720,
    7,
    4,
    16,
    4,
    168,
    166,
    4,
    1,
    Device::RotationStepMode::FlipOnly,
    0,
    2,
    Device::Capabilities{false, false, false, false, false, false, kBuiltinCamera},
    kHardwareIoProfile,
};

bool init();
void update();

void displayPushPixels(int32_t x, int32_t y, int32_t w, int32_t h,
                       const uint16_t* data);
void displayPushPixelsDMA(int32_t x, int32_t y, int32_t w, int32_t h,
                          const uint16_t* data);
bool displayTryFullFramePreview(int32_t x, int32_t y, int32_t w, int32_t h,
                                int32_t source_stride,
                                const uint16_t* data, size_t data_size,
                                bool byte_swap);
void displayWaitDMA();
void displayFillScreen(uint16_t color);
void displaySetRotation(uint8_t rotation);

void setBrightness(uint8_t value);
uint8_t getBrightness();

bool getTouch(int16_t& x, int16_t& y);

void displaySleep();
void displayWake();
void displayWakeDark();
void displayPowerSaveOn();
void displayPowerSaveOff();
void displayWaitDisplay();
void prepareForRestart();

bool initSDCard();
bool storageReady();
fs::FS& storageFS();

bool sdReady();
fs::FS& sdFS();
bool suspendSDCardForNetworkTransition();
bool resumeSDCardAfterNetworkTransition();

bool initLittleFS();
void migrateStorageFromSD();

}  // namespace DeviceM5StacksTab5
