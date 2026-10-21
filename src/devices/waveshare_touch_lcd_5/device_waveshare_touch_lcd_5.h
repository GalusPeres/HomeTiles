#pragma once

#include "src/devices/waveshare_touch_lcd_5/dsi_driver/waveshare_p4_dsi.h"
#include "src/devices/waveshare_touch_lcd_5/profile.h"

// Waveshare ESP32-P4-WIFI6-Touch-LCD-5: the shared Waveshare ESP32-P4
// MIPI-DSI driver runs it from its description (board.h). This board only
// adds the HX8394 panel controller (vendor/hx8394) and its facts.
namespace DeviceWaveshareTouchLCD5 {

inline constexpr Device::Profile kProfile =
    DeviceWaveshareTouchLCD5Profile::kProfile;

using waveshare_p4_dsi::init;
using waveshare_p4_dsi::update;
using waveshare_p4_dsi::displayPushPixels;
using waveshare_p4_dsi::displayPushPixelsDMA;
using waveshare_p4_dsi::displayTryFullFramePreview;
using waveshare_p4_dsi::displayEndFullFramePreview;
using waveshare_p4_dsi::displayFullFrameInfo;
using waveshare_p4_dsi::displayBeginFullFrames;
using waveshare_p4_dsi::displayAcquireFullFrame;
using waveshare_p4_dsi::displaySubmitFullFrame;
using waveshare_p4_dsi::displayEndFullFrames;
using waveshare_p4_dsi::displayWaitDMA;
using waveshare_p4_dsi::displayCommit;
using waveshare_p4_dsi::displayFillScreen;
using waveshare_p4_dsi::displaySetRotation;
using waveshare_p4_dsi::displaySetUpright;
using waveshare_p4_dsi::pausePpaFor;
using waveshare_p4_dsi::ppaCooldownActive;
using waveshare_p4_dsi::setBrightness;
using waveshare_p4_dsi::getBrightness;
using waveshare_p4_dsi::getTouch;
using waveshare_p4_dsi::displaySleep;
using waveshare_p4_dsi::displayWake;
using waveshare_p4_dsi::displayWakeDark;
using waveshare_p4_dsi::displayPowerSaveOn;
using waveshare_p4_dsi::displayPowerSaveOff;
using waveshare_p4_dsi::displayWaitDisplay;
using waveshare_p4_dsi::prepareForRestart;
using waveshare_p4_dsi::sharedI2cBus;
using waveshare_p4_dsi::initSDCard;
using waveshare_p4_dsi::storageReady;
using waveshare_p4_dsi::storageFS;
using waveshare_p4_dsi::sdReady;
using waveshare_p4_dsi::sdFS;
using waveshare_p4_dsi::suspendSDCardForNetworkTransition;
using waveshare_p4_dsi::resumeSDCardAfterNetworkTransition;
using waveshare_p4_dsi::initLittleFS;
using waveshare_p4_dsi::migrateStorageFromSD;

}  // namespace DeviceWaveshareTouchLCD5
