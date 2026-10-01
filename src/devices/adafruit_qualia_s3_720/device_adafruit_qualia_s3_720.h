#pragma once

#include <FS.h>

#include "src/devices/device_types.h"
#include "src/devices/adafruit_qualia_s3_720/hardware_io_profile.h"

namespace DeviceAdafruitQualiaS3720 {

// Adafruit Qualia ESP32-S3 for RGB-666 displays (product 5800) with the 4"
// square 720x720 TL040HDS20 panel (product 5794). The grid matches the
// 720x720 Waveshare B4 layout: 4 * 166 + 3 * 16 + 2 * 4 = 720.
inline constexpr Device::Profile kProfile{
    "adafruit_qualia_s3_720",
    "Adafruit Qualia ESP32-S3 720x720",
    720,
    720,
    4,
    4,
    16,
    4,
    166,
    166,
    4,
    // The backlight is a TCA9554 expander output without PWM: any non-zero
    // level switches it on, raw 0 switches it off.
    1,
    Device::RotationStepMode::QuarterTurns,
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
bool displayTryFullFramePreview(int32_t x, int32_t y, int32_t w, int32_t h,
                                int32_t source_stride,
                                const uint16_t* data, size_t data_size,
                                bool byte_swap);
bool displayBeginAtomicFrame(const char* reason);
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
void storageWriteBegin();
void storageWriteEnd();

bool sdReady();
fs::FS& sdFS();
bool suspendSDCardForNetworkTransition();
bool resumeSDCardAfterNetworkTransition();

bool initLittleFS();
void migrateStorageFromSD();

}  // namespace DeviceAdafruitQualiaS3720
