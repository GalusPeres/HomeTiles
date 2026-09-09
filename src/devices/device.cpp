#include "src/devices/device.h"

namespace Device {

namespace {

bool g_sd_ready_cached = false;

}  // namespace

const Profile& profile() {
  return kProfile;
}

const char* displayName() {
  return kProfile.display_name;
}

bool init() {
  return DeviceImpl::init();
}

void update() {
  DeviceImpl::update();
}

void displayPushPixels(int32_t x, int32_t y, int32_t w, int32_t h,
                       const uint16_t* data) {
  DeviceImpl::displayPushPixels(x, y, w, h, data);
}

void displayPushPixelsDMA(int32_t x, int32_t y, int32_t w, int32_t h,
                          const uint16_t* data) {
  DeviceImpl::displayPushPixelsDMA(x, y, w, h, data);
}

bool displayTryFullFramePreview(int32_t x, int32_t y, int32_t w, int32_t h,
                                int32_t source_stride,
                                const uint16_t* data, size_t data_size,
                                bool byte_swap) {
  return DeviceImpl::displayTryFullFramePreview(
      x, y, w, h, source_stride, data, data_size, byte_swap);
}

void displayEndFullFramePreview() {
#if defined(DEVICE_P4_IDF_DSI)
  DeviceImpl::displayEndFullFramePreview();
#endif
}

bool ppaCooldownActive() {
#if defined(DEVICE_WAVESHARE_TOUCH_LCD_X) || \
    defined(DEVICE_GUITION_JC1060P470C_FAMILY) || \
    defined(DEVICE_GUITION_JC8012P4A1) || \
    defined(DEVICE_GUITION_JC8012P4A1_V2)
  return DeviceImpl::ppaCooldownActive();
#else
  return false;
#endif
}

void displayWaitDMA() {
  DeviceImpl::displayWaitDMA();
}

void displayWaitFrameStart() {
#if defined(DEVICE_WAVESHARE_TOUCH_LCD_8)
  DeviceImpl::displayWaitFrameStart();
#endif
}

void displayFillScreen(uint16_t color) {
  DeviceImpl::displayFillScreen(color);
}

void displaySetRotation(uint8_t rotation) {
  DeviceImpl::displaySetRotation(rotation);
}

void setBrightness(uint8_t value) {
  DeviceImpl::setBrightness(value);
}

uint8_t getBrightness() {
  return DeviceImpl::getBrightness();
}

bool getTouch(int16_t& x, int16_t& y) {
  return DeviceImpl::getTouch(x, y);
}

void displaySleep() {
  DeviceImpl::displaySleep();
}

void displayWake() {
  DeviceImpl::displayWake();
}

void displayWakeDark() {
#if defined(DEVICE_WAVESHARE_TOUCH_LCD_X) || \
    defined(DEVICE_M5STACKS_TAB5) || \
    defined(DEVICE_GUITION_JC8012P4A1_FAMILY) || \
    defined(DEVICE_GUITION_JC1060P470C_FAMILY) || \
    defined(DEVICE_ESP32_S3_RGB_480)
  DeviceImpl::displayWakeDark();
#else
  DeviceImpl::displayWake();
  DeviceImpl::setBrightness(0);
#endif
}

void displayPowerSaveOn() {
  DeviceImpl::displayPowerSaveOn();
}

void displayPowerSaveOff() {
  DeviceImpl::displayPowerSaveOff();
}

void displayWaitDisplay() {
  DeviceImpl::displayWaitDisplay();
}

void prepareForRestart() {
  DeviceImpl::prepareForRestart();
}

void displayUpdateCheckGuardBegin() {
#if defined(DEVICE_GUITION_ESP32_4848S040)
  DeviceImpl::displayUpdateCheckGuardBegin();
#endif
}

void displayUpdateCheckGuardEnd() {
#if defined(DEVICE_GUITION_ESP32_4848S040)
  DeviceImpl::displayUpdateCheckGuardEnd();
#endif
}

bool initSDCard() {
  g_sd_ready_cached = DeviceImpl::initSDCard();
  return g_sd_ready_cached;
}

bool storageReady() {
  return DeviceImpl::storageReady();
}

fs::FS& storageFS() {
  return DeviceImpl::storageFS();
}

void storageWriteBegin() {
#if defined(DEVICE_ESP32_S3_RGB_480)
  DeviceImpl::storageWriteBegin();
#endif
}

void storageWriteEnd() {
#if defined(DEVICE_ESP32_S3_RGB_480)
  DeviceImpl::storageWriteEnd();
#endif
}

bool sdReady() {
  g_sd_ready_cached = DeviceImpl::sdReady();
  return g_sd_ready_cached;
}

bool sdReadyCached() {
  return g_sd_ready_cached;
}

bool sdWritable() {
#if defined(DEVICE_GUITION_JC1060P470C_FAMILY)
  const bool writable = DeviceImpl::sdWritable();
  if (writable) g_sd_ready_cached = true;
  return writable;
#else
  return sdReady();
#endif
}

fs::FS& sdFS() {
  return DeviceImpl::sdFS();
}

bool suspendSDCardForNetworkTransition() {
  const bool suspended = DeviceImpl::suspendSDCardForNetworkTransition();
  // The storage backend is unavailable throughout the transition even when
  // there was no mounted card to suspend.
  g_sd_ready_cached = false;
  return suspended;
}

bool resumeSDCardAfterNetworkTransition() {
  g_sd_ready_cached = DeviceImpl::resumeSDCardAfterNetworkTransition();
  return g_sd_ready_cached;
}

bool initLittleFS() {
  return DeviceImpl::initLittleFS();
}

void migrateStorageFromSD() {
  DeviceImpl::migrateStorageFromSD();
}

uint8_t normalizeRotationQuarterTurns(uint8_t rotation) {
  rotation &= 0x03;
  if (kRotationStepMode == RotationStepMode::FlipOnly) {
    return (rotation & 0x02);
  }
  return rotation;
}

bool supportsQuarterTurnRotation() {
  return kRotationStepMode == RotationStepMode::QuarterTurns;
}

}  // namespace Device
