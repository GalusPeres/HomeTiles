#pragma once

#include <FS.h>
#include <stddef.h>
#include <stdint.h>

#include "src/devices/panlee_zx3d95ce01s/gc9503v_s3_board.h"

// API of the Panlee ZX3D95CE01S GC9503V RGB driver (device_panlee_zx3d95ce01s.cpp),
// derived from the GS-T3E v2 driver, which stays unchanged. It
// runs with the kBoard description of device_panlee_zx3d95ce01s.h.
namespace DevicePanleeZX3D95CE01S {

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
// Prepare the inactive RGB framebuffer for one tear-free full-screen redraw.
// Normal partial UI updates continue to use the active framebuffer directly.
bool displayBeginAtomicFrame(const char* reason);
// Temporarily reduce RGB scanout bandwidth around the HTTPS version check.
void displayUpdateCheckGuardBegin();
void displayUpdateCheckGuardEnd();
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

}  // namespace DevicePanleeZX3D95CE01S
