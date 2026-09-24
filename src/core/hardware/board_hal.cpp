#include "src/core/hardware/board_hal.h"

#include "src/devices/device.h"
#include "src/devices/device_select.h"
#include "src/video/local_camera/local_camera.h"

#if defined(DEVICE_WAVESHARE_4B)
#include <esp_private/system_internal.h>
#endif
#if defined(DEVICE_ESP32_S3_RGB_480)
#include <esp_sleep.h>
#endif

bool BoardHAL::init() {
  return Device::init();
}

void BoardHAL::update() {
  Device::update();
}

void BoardHAL::displayPushPixels(int32_t x, int32_t y, int32_t w, int32_t h,
                                 const uint16_t* data) {
  Device::displayPushPixels(x, y, w, h, data);
}

void BoardHAL::displayPushPixelsDMA(int32_t x, int32_t y, int32_t w, int32_t h,
                                    const uint16_t* data) {
  Device::displayPushPixelsDMA(x, y, w, h, data);
}

void BoardHAL::displayWaitDMA() {
  Device::displayWaitDMA();
}

void BoardHAL::displayFillScreen(uint16_t color) {
  Device::displayFillScreen(color);
}

void BoardHAL::displaySetRotation(uint8_t rotation) {
  Device::displaySetRotation(rotation);
}

void BoardHAL::setBrightness(uint8_t value) {
  Device::setBrightness(value);
}

uint8_t BoardHAL::getBrightness() {
  return Device::getBrightness();
}

bool BoardHAL::getTouch(TouchPoint* tp) {
  if (!tp) {
    return false;
  }
  return Device::getTouch(tp->x, tp->y);
}

void BoardHAL::displaySleep() {
  Device::displaySleep();
}

void BoardHAL::displayWake() {
  Device::displayWake();
}

void BoardHAL::displayWakeDark() {
  Device::displayWakeDark();
}

void BoardHAL::displayPowerSaveOn() {
  Device::displayPowerSaveOn();
}

void BoardHAL::displayPowerSaveOff() {
  Device::displayPowerSaveOff();
}

void BoardHAL::displayWaitDisplay() {
  Device::displayWaitDisplay();
}

void BoardHAL::prepareForRestart() {
  // Put a built-in camera into software standby before the chip resets.
  local_camera::shutdown("restart");
  Device::prepareForRestart();
}

void BoardHAL::restart() {
  Serial.flush();
#if defined(DEVICE_WAVESHARE_4B)
  Serial.println("[BoardHAL] Restart via no-OS restart");
  Serial.flush();
  esp_restart_noos();
#elif defined(DEVICE_ESP32_S3_RGB_480)
  // A timed deep-sleep wake can resume the currently running OTA image even
  // after Update.end() selected the other slot. Use a real chip restart so
  // the second-stage bootloader evaluates otadata and boots the new image.
  Serial.println("[BoardHAL] ESP32-S3 restart via ESP.restart");
  Serial.flush();
  ESP.restart();
#else
  ESP.restart();
#endif
}

bool BoardHAL::initSDCard() {
  return Device::initSDCard();
}
