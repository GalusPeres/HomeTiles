#pragma once

#include <FS.h>

#include "src/devices/waveshare_touch_lcd_7/profile.h"

// Same typedef as ESP-IDF's i2c_types.h; host tests include this header
// without the IDF drivers.
typedef struct i2c_master_bus_t* i2c_master_bus_handle_t;

namespace DeviceWaveshareTouchLCD7 {

inline constexpr Device::Profile kProfile =
    DeviceWaveshareTouchLCD7Profile::kProfile;

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
void displayWaitDMA();
void displayCommit();
void displayFillScreen(uint16_t color);
void displaySetRotation(uint8_t rotation);
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
// The board I2C bus (touch, camera SCCB) once init() created it, else nullptr.
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

}  // namespace DeviceWaveshareTouchLCD7
