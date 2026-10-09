#pragma once

#include <FS.h>

#include "src/devices/device_types.h"
#include "src/devices/guition_jc1060p470c/hardware_io_profile.h"

namespace DeviceGuitionJC1060P470C {

inline constexpr Device::Profile kProfile{
    "guition_jc1060p470c",
    "Guition JC1060P470C",
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
// Best-effort full-image preview straight into the physical framebuffer.
// Uses only the already registered, timeout-protected PPA client and never
// falls back to a full CPU rotation. false leaves the normal LVGL path in
// charge, which completes or replaces any partial preview.
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

bool initSDCard();
bool storageReady();
fs::FS& storageFS();

bool sdReady();
bool sdWritable();
fs::FS& sdFS();
bool suspendSDCardForNetworkTransition();
bool resumeSDCardAfterNetworkTransition();

bool initLittleFS();
void migrateStorageFromSD();

}  // namespace DeviceGuitionJC1060P470C
