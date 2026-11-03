#pragma once

#include <FS.h>

#include "src/devices/device_types.h"
#include "src/devices/btt_panda_touch/hardware_io_profile.h"

namespace DeviceBTTPandaTouch {

// BIGTREETECH Panda Touch (4.3 inch, 800 x 480):
// ESP32-S3 with 16 MB flash and 8 MB octal PSRAM, an 800 x 480 RGB565 panel
// in DE mode and a GT900 capacitive touch controller on I2C0.
inline constexpr Device::Profile kProfile{
    "btt_panda_touch",
    "BTT Panda Touch",
    800,
    480,
    4,
    3,
    10,
    3,
    187,
    146,
    4,
    // The backlight is a plain LEDC PWM line; every duty from 1 % up is
    // visible, so the raw scale starts at 1.
    1,
    // The panel is physically landscape: only the flip (180 degrees) fits.
    Device::RotationStepMode::FlipOnly,
    0,
    2,
    Device::Capabilities{false, false, false, false, false, false},
    kHardwareIoProfile,
};

bool init();
void update();

void displayPushPixels(int32_t x, int32_t y, int32_t w, int32_t h,
                       const uint16_t* data);
void displayPushPixelsDMA(int32_t x, int32_t y, int32_t w, int32_t h,
                          const uint16_t* data);
// The camera tiles need a decoding pipeline this board does not have.
bool displayTryFullFramePreview(int32_t x, int32_t y, int32_t w, int32_t h,
                                int32_t source_stride,
                                const uint16_t* data, size_t data_size,
                                bool byte_swap);
void displayWaitDMA();
void displayFillScreen(uint16_t color);
void displaySetRotation(uint8_t rotation);
// The panel scans a single PSRAM framebuffer directly, so there is no second
// buffer to render the UI into. The caller blanks the screen instead.
bool displayBeginAtomicFrame(const char* reason);

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

// LittleFS is the only storage on this board; there is no microSD slot.
bool initSDCard();
bool storageReady();
fs::FS& storageFS();
void storageWriteBegin();
void storageWriteEnd();

bool sdReady();
fs::FS& sdFS();
bool suspendSDCardForNetworkTransition();
bool resumeSDCardAfterNetworkTransition();

bool initLittleFS();
void migrateStorageFromSD();

}  // namespace DeviceBTTPandaTouch
