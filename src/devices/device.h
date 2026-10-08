#pragma once

#include <FS.h>

#include "src/devices/active_device.h"
#include "src/devices/device_types.h"

namespace Device {

inline constexpr Profile kProfile = DeviceImpl::kProfile;
inline constexpr uint16_t kScreenWidth = kProfile.screen_width;
inline constexpr uint16_t kScreenHeight = kProfile.screen_height;
inline constexpr uint8_t kGridCols = kProfile.grid_cols;
inline constexpr uint8_t kGridRows = kProfile.grid_rows;
inline constexpr uint16_t kGridGap = kProfile.grid_gap;
inline constexpr uint16_t kGridPad = kProfile.grid_pad;
inline constexpr uint16_t kGridCellW = kProfile.grid_cell_w;
inline constexpr uint16_t kGridCellH = kProfile.grid_cell_h;
inline constexpr uint8_t kDisplayFlushBands = kProfile.display_flush_bands;
inline constexpr uint8_t kBacklightInputMin = kProfile.backlight_input_min;
#if defined(DEVICE_WAVESHARE_TOUCH_LCD_10_1)
inline constexpr uint8_t kConfiguredBrightnessPercentMin = 2;
#else
inline constexpr uint8_t kConfiguredBrightnessPercentMin = 1;
#endif
inline constexpr RotationStepMode kRotationStepMode = kProfile.rotation_step_mode;
inline constexpr uint8_t kRotationDefault = kProfile.rotation_default;
inline constexpr uint8_t kRotationFlipped = kProfile.rotation_flipped;
inline constexpr Capabilities kCapabilities = kProfile.capabilities;

// Use consistent percentages for normal display brightness, screensaver and
// MQTT. Keep storing the device-specific raw value for compatibility.
inline uint8_t backlightRawFromPercent(uint8_t percent) {
  if (percent < kConfiguredBrightnessPercentMin) {
    percent = kConfiguredBrightnessPercentMin;
  }
  if (percent > 100) percent = 100;
  const uint16_t span = static_cast<uint16_t>(255U - kBacklightInputMin);
  return static_cast<uint8_t>(
      kBacklightInputMin +
      (static_cast<uint32_t>(percent - 1U) * span + 49U) / 99U);
}

inline uint8_t backlightPercentFromRaw(uint8_t raw) {
  if (raw < kBacklightInputMin) raw = kBacklightInputMin;
  const uint16_t span = static_cast<uint16_t>(255U - kBacklightInputMin);
  if (span == 0) return 100;
  uint8_t percent = static_cast<uint8_t>(
      1U + (static_cast<uint32_t>(raw - kBacklightInputMin) * 99U +
            (span / 2U)) /
               span);
  if (percent < kConfiguredBrightnessPercentMin) {
    percent = kConfiguredBrightnessPercentMin;
  }
  return percent;
}

const Profile& profile();
const char* displayName();

bool init();
void update();

void displayPushPixels(int32_t x, int32_t y, int32_t w, int32_t h,
                       const uint16_t* data);
void displayPushPixelsDMA(int32_t x, int32_t y, int32_t w, int32_t h,
                          const uint16_t* data);
// Shared direct frame path for the camera and fullscreen preview. The source
// stride is explicit because the P4 JPEG decoder aligns rows to 16 pixels.
// false means skip the frame; never silently redraw it through LVGL.
bool displayTryFullFramePreview(int32_t x, int32_t y, int32_t w, int32_t h,
                                int32_t source_stride,
                                const uint16_t* data, size_t data_size,
                                bool byte_swap);
// Ends device-specific preview buffering. This is a no-op on devices whose
// preview path does not keep persistent display state.
void displayEndFullFramePreview();
bool ppaCooldownActive();
void displayWaitDMA();
// Best-effort wait for the beginning of a physical panel frame. Devices
// without a refresh callback implement this as a no-op.
void displayWaitFrameStart();
void displayFillScreen(uint16_t color);
void displaySetRotation(uint8_t rotation);
// The upright layout on a landscape panel (grid_layout::turned()): LVGL draws
// the screen upright and the panel shows it without the quarter turn; touch
// comes upright too. Only panels with grid_layout::upright_ready() act on it.
void displaySetUpright(bool upright);

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

// Exact-device guard for network work that can contend with RGB scanout.
// It is a no-op on profiles without a proven hardware requirement.
void displayUpdateCheckGuardBegin();
void displayUpdateCheckGuardEnd();

bool initSDCard();
bool storageReady();
fs::FS& storageFS();
// Bracket write-heavy storage transactions. Most devices implement this as a
// no-op; the ESP32-S3 RGB board uses it to hide unavoidable main-flash stalls
// until the continuously scanned panel has re-synchronised.
void storageWriteBegin();
void storageWriteEnd();

class ScopedStorageWrite {
 public:
  explicit ScopedStorageWrite(bool active = true) : active_(active) {
    if (active_) storageWriteBegin();
  }
  ~ScopedStorageWrite() {
    if (active_) storageWriteEnd();
  }

  ScopedStorageWrite(const ScopedStorageWrite&) = delete;
  ScopedStorageWrite& operator=(const ScopedStorageWrite&) = delete;

 private:
  bool active_;
};

bool sdReady();
// Returns the last SD readiness result without mounting or probing the card.
// Use this in latency-sensitive UI paths after the normal boot-time probe.
bool sdReadyCached();
// True when the mounted SD backend has passed a real metadata/data write
// check. Other profiles retain their established sdReady() semantics.
bool sdWritable();
fs::FS& sdFS();
bool suspendSDCardForNetworkTransition();
bool resumeSDCardAfterNetworkTransition();

bool initLittleFS();
void migrateStorageFromSD();

uint8_t normalizeRotationQuarterTurns(uint8_t rotation);
bool supportsQuarterTurnRotation();

}  // namespace Device
