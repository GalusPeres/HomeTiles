#pragma once

#include <FS.h>
#include <stddef.h>
#include <stdint.h>

#include "src/devices/golden_security_gs_t3e_v24/st7701_s3_board.h"

// API of the GS-T3E v2.4 ST7701 RGB driver (device_golden_security_gs_t3e_v24.cpp),
// derived from the Guition ESP32-4848S040 driver. It runs with the kBoard
// description of device_golden_security_gs_t3e_v24.h.
namespace DeviceGoldenSecurityGST3EV24 {

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

}  // namespace DeviceGoldenSecurityGST3EV24
