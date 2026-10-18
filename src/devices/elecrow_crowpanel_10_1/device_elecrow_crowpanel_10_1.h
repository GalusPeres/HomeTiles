#pragma once

#include <FS.h>

#include "src/devices/device_select.h"
#include "src/devices/device_types.h"
#include "src/devices/elecrow_crowpanel_10_1/hardware_io_profile.h"

// Same typedef as ESP-IDF's i2c_types.h; host tests include this header
// without the IDF drivers.
typedef struct i2c_master_bus_t* i2c_master_bus_handle_t;

namespace DeviceElecrowCrowPanel10 {

inline constexpr Device::Profile kProfile{
    "elecrow_crowpanel_10_1",
    "Elecrow CrowPanel Advanced 10.1",
    1024,
    600,
    6,
    4,
    16,
    4,
    156,
    136,
    5,
    121,
    Device::RotationStepMode::FlipOnly,
    2,
    0,
    Device::Capabilities{false, false, false, false, true, false, false},
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
void displayEndFullFramePreview();
bool displayFullFrameInfo(uint16_t& width, uint16_t& height, uint16_t& turn_cw);
bool displayBeginFullFrames();
uint16_t* displayAcquireFullFrame(size_t& bytes);
bool displaySubmitFullFrame();
void displayEndFullFrames();
void displayWaitDMA();
void displayWaitDisplayStart();
void displayCommit();
void displayFillScreen(uint16_t color);
void displaySetRotation(uint8_t rotation);
void displaySetUpright(bool upright);
void pausePpaFor(uint32_t duration_ms);
bool ppaCooldownActive();

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

i2c_master_bus_handle_t sharedI2cBus();

bool initSDCard();
bool storageReady();
fs::FS& storageFS();

bool sdReady();
fs::FS& sdFS();
bool suspendSDCardForNetworkTransition();
bool resumeSDCardAfterNetworkTransition();

bool initLittleFS();
void migrateStorageFromSD();

}  // namespace DeviceElecrowCrowPanel10
