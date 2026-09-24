#pragma once

#include <FS.h>

#include "src/devices/device_select.h"
#include "src/devices/device_types.h"
#include "src/devices/waveshare_touch_lcd_8/hardware_io_profile.h"

// Same typedef as ESP-IDF's i2c_types.h; host tests include this header
// without the IDF drivers.
typedef struct i2c_master_bus_t* i2c_master_bus_handle_t;

namespace DeviceWaveshareTouchLCD8 {

// Built-in OV5647 front camera: enabled only in camera beta builds
// (HOMETILES_LOCAL_CAMERA in device_select.h) until hardware validation.
#if defined(HOMETILES_LOCAL_CAMERA)
inline constexpr bool kBuiltinCamera = true;
#else
inline constexpr bool kBuiltinCamera = false;
#endif

inline constexpr Device::Profile kProfile{
    "waveshare_touch_lcd_8",
    "Waveshare Touch LCD 8",
    1280,
    800,
    7,
    5,
    16,
    4,
    168,
    145,
    5,
    121,
    Device::RotationStepMode::FlipOnly,
    0,
    2,
    Device::Capabilities{false, false, false, false, true, false, kBuiltinCamera},
    kHardwareIoProfile,
};

bool init();
void update();

void displayPushPixels(int32_t x, int32_t y, int32_t w, int32_t h,
                       const uint16_t* data);
void displayPushPixelsDMA(int32_t x, int32_t y, int32_t w, int32_t h,
                          const uint16_t* data);
// Best-effort full-image preview straight into the physical framebuffer.
// Uses only the already registered, timeout-protected PPA client and never
// falls back to a full CPU rotation. false leaves the normal LVGL path in
// charge, which completes or replaces any partial preview.
bool displayTryFullFramePreview(int32_t x, int32_t y, int32_t w, int32_t h,
                                int32_t source_stride,
                                const uint16_t* data, size_t data_size,
                                bool byte_swap);
void displayEndFullFramePreview();
void displayWaitDMA();
void displayWaitFrameStart();
void displayCommit();
void displayFillScreen(uint16_t color);
void displaySetRotation(uint8_t rotation);
void pausePpaFor(uint32_t duration_ms);
// Read-only: true while a pausePpaFor() cooldown is still active (all flushes
// fall back to slow CPU rotate during this window). Lets callers that redraw
// frequently (e.g. the pixel-animation tile) hold their current frame instead
// of fighting the cooldown for the CPU-rotate path.
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

}  // namespace DeviceWaveshareTouchLCD8
