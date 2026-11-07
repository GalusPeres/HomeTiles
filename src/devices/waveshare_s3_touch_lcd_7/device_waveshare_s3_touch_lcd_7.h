#pragma once

#include <FS.h>

#include "src/devices/device_types.h"
#include "src/devices/waveshare_s3_touch_lcd_7/hardware_io_profile.h"

namespace DeviceWaveshareS3TouchLCD7 {

inline constexpr Device::Profile kProfile{
    "waveshare_s3_touch_lcd_7",
    "Waveshare ESP32-S3-Touch-LCD-7",
    800,
    480,
    4,
    4,
    10,
    3,
    111,
    111,
    4,
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

}  // namespace DeviceWaveshareS3TouchLCD7
