#include "src/devices/device_select.h"

#if defined(DEVICE_BTT_PANDA_TOUCH)

#include <Arduino.h>
#include <LittleFS.h>
#include <Wire.h>
#include <esp_cache.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>

#include <cstring>

namespace DeviceBTTPandaTouch {

namespace {

// ---------------------------------------------------------------------------
// Board facts (measured on the hardware, see README.md)
// ---------------------------------------------------------------------------

constexpr int16_t kScreenWidth = 800;
constexpr int16_t kScreenHeight = 480;

// The panel runs in DE mode: the horizontal and vertical synchronisation
// lines are not wired, only the data enable and the pixel clock.
constexpr int8_t kPanelPclk = 5;
constexpr int8_t kPanelDe = 38;
constexpr int8_t kPanelReset = 46;

// The RGB bus carries B3..B7, G2..G7 and R3..R7 on sixteen lines.
constexpr int8_t kPanelData[16] = {17, 18, 48, 47, 39, 11, 12, 13,
                                   14, 15, 16, 6,  7,  8,  9,  10};

// 23 MHz is the clock of the board's own display service.
constexpr uint32_t kPanelPclkHz = 23 * 1000 * 1000;

constexpr int8_t kBacklightPin = 21;
// The backlight is a plain LEDC PWM line. A fast rate sits in the range the
// ear hears best and makes the panel whine whenever the backlight is dimmed,
// so this board uses its own low, silent rate.
constexpr uint32_t kBacklightFrequency = 200;
constexpr uint8_t kBacklightResolution = 10;
constexpr uint32_t kBacklightMaxDuty = (1u << kBacklightResolution) - 1u;

constexpr int8_t kTouchSda = 2;
constexpr int8_t kTouchScl = 1;
constexpr int8_t kTouchInt = 40;
constexpr int8_t kTouchReset = 41;
constexpr uint32_t kTouchFrequency = 400000;
constexpr uint8_t kTouchAddressPrimary = 0x5D;
constexpr uint8_t kTouchAddressAlternate = 0x14;
// The controller on this board is a GT900 (it reports "GOODIX_GT900_105"),
// which speaks the GT911 register protocol.
constexpr uint16_t kTouchProductIdRegister = 0x8140;
constexpr uint16_t kTouchStatusRegister = 0x814E;
// The point block starts with the coordinates, the track id comes later.
constexpr uint16_t kTouchPointRegister = 0x8150;
// A held point survives the polls between two controller frames. Real I2C
// errors release it after this many consecutive failures.
constexpr uint8_t kTouchErrorReleaseThreshold = 16;


esp_lcd_panel_handle_t g_panel = nullptr;
uint16_t* g_framebuffer = nullptr;
uint16_t* g_framebuffers[2] = {nullptr, nullptr};
// The buffer the panel is scanning right now. The UI is rendered into the
// other one and the panel switches at the frame boundary.
uint8_t g_displayedBuffer = 0;
bool g_display_ready = false;
bool g_touch_ready = false;
bool g_littlefs_ready = false;

uint8_t g_rotation = 0;
uint8_t g_brightness = 160;
uint8_t g_applied_brightness = 0;
bool g_backlight_ready = false;

uint8_t g_touch_address = 0;
bool g_touch_active = false;
int16_t g_touch_x = 0;
int16_t g_touch_y = 0;
uint8_t g_touch_error_streak = 0;

uint16_t g_storage_write_depth = 0;
uint8_t g_storage_restore_brightness = 0;
bool g_storage_blackout_active = false;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void applyBrightness(uint8_t value) {
  g_brightness = value;
  if (!g_backlight_ready) return;
  uint32_t duty = 0;
  if (value > 0) {
    const uint32_t span = kBacklightMaxDuty;
    duty = (static_cast<uint32_t>(value) * span + 127U) / 255U;
  }
  ledcWrite(kBacklightPin, duty);
  g_applied_brightness = value;
}

void backlightInit() {
  ledcAttach(kBacklightPin, kBacklightFrequency, kBacklightResolution);
  g_backlight_ready = true;
  applyBrightness(g_brightness);
}

// The CPU cache holds the framebuffer writes, but the LCD reads the buffer
// from memory. Push the dirty lines out. The rows of the framebuffer are not
// contiguous, so a part of a row has to be synced on its own; a band that
// covers the full width is one continuous block and needs a single call.
void syncRegion(int32_t x, int32_t y, int32_t w, int32_t h) {
  if (!g_framebuffer || w <= 0 || h <= 0) return;
  if (x <= 0 && w >= kScreenWidth) {
    uint16_t* first = g_framebuffer + static_cast<size_t>(y) * kScreenWidth;
    esp_cache_msync(first,
                    static_cast<size_t>(h) * kScreenWidth * sizeof(uint16_t),
                    ESP_CACHE_MSYNC_FLAG_DIR_C2M |
                        ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    return;
  }
  for (int32_t row = y; row < y + h; ++row) {
    uint16_t* line = g_framebuffer + static_cast<size_t>(row) * kScreenWidth + x;
    esp_cache_msync(line, static_cast<size_t>(w) * sizeof(uint16_t),
                    ESP_CACHE_MSYNC_FLAG_DIR_C2M |
                        ESP_CACHE_MSYNC_FLAG_UNALIGNED);
  }
}

bool writeTouchRegister(uint16_t reg, uint8_t value) {
  if (!g_touch_address) return false;
  Wire.beginTransmission(g_touch_address);
  Wire.write(static_cast<uint8_t>(reg >> 8));
  Wire.write(static_cast<uint8_t>(reg & 0xFF));
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool readTouchRegisters(uint16_t reg, uint8_t* data, size_t len) {
  if (!g_touch_address || !data || len == 0 || len > 32) return false;
  Wire.beginTransmission(g_touch_address);
  Wire.write(static_cast<uint8_t>(reg >> 8));
  Wire.write(static_cast<uint8_t>(reg & 0xFF));
  if (Wire.endTransmission(false) != 0) return false;
  const size_t received =
      Wire.requestFrom(static_cast<int>(g_touch_address), static_cast<int>(len));
  if (received != len) {
    while (Wire.available()) Wire.read();
    return false;
  }
  for (size_t i = 0; i < len; ++i) data[i] = Wire.read();
  return true;
}

bool initTouch() {
  if (g_touch_ready) return true;
  Wire.begin(kTouchSda, kTouchScl, kTouchFrequency);
  for (uint8_t address : {kTouchAddressPrimary, kTouchAddressAlternate}) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() != 0) continue;
    g_touch_address = address;
    break;
  }
  if (!g_touch_address) {
    Serial.println("[Device/BTT Panda Touch] Touch controller not found");
    return false;
  }

  // Reset the controller the way the GT900 datasheet asks for: the interrupt
  // line is pulled low while the reset line pulses.
  pinMode(kTouchInt, OUTPUT);
  digitalWrite(kTouchInt, LOW);
  pinMode(kTouchReset, OUTPUT);
  digitalWrite(kTouchReset, LOW);
  delay(20);
  digitalWrite(kTouchReset, HIGH);
  delay(10);
  pinMode(kTouchInt, INPUT);
  delay(120);

  uint8_t id[4] = {0, 0, 0, 0};
  readTouchRegisters(kTouchProductIdRegister, id, sizeof(id));
  Serial.printf("[Device/BTT Panda Touch] Touch controller at 0x%02X, id=%c%c%c%c\n",
                g_touch_address, id[0], id[1], id[2], id[3]);
  g_touch_ready = true;
  return true;
}

bool initDisplay() {
  pinMode(kPanelReset, OUTPUT);
  digitalWrite(kPanelReset, LOW);
  delay(100);
  digitalWrite(kPanelReset, HIGH);
  delay(100);

  esp_lcd_rgb_panel_config_t config{};
  config.clk_src = LCD_CLK_SRC_DEFAULT;
  config.timings.pclk_hz = kPanelPclkHz;
  config.timings.h_res = kScreenWidth;
  config.timings.v_res = kScreenHeight;
  config.timings.hsync_pulse_width = 4;
  config.timings.hsync_back_porch = 8;
  config.timings.hsync_front_porch = 8;
  config.timings.vsync_pulse_width = 4;
  config.timings.vsync_back_porch = 16;
  config.timings.vsync_front_porch = 16;
  config.timings.flags.pclk_active_neg = true;
  config.data_width = 16;
  config.bits_per_pixel = 16;
  // The panel's DMA reads the picture from a bounce buffer in DRAM: the
  // ESP32-S3 in this SDK build has no PSRAM XIP and 32-byte cache lines, and
  // a DMA that reads the PSRAM framebuffer directly loses its sync under
  // load. The driver copies every line into the bounce buffer and
  // invalidates the CPU cache while it does so.
  config.num_fbs = 1;
  config.bounce_buffer_size_px = kScreenWidth * 20;
  config.flags.bb_invalidate_cache = true;
  config.flags.double_fb = false;
  config.hsync_gpio_num = GPIO_NUM_NC;
  config.vsync_gpio_num = GPIO_NUM_NC;
  config.de_gpio_num = kPanelDe;
  config.pclk_gpio_num = kPanelPclk;
  config.disp_gpio_num = GPIO_NUM_NC;
  for (int i = 0; i < 16; ++i) config.data_gpio_nums[i] = kPanelData[i];
  config.flags.fb_in_psram = true;
  config.flags.no_fb = false;

  esp_err_t err = esp_lcd_new_rgb_panel(&config, &g_panel);
  if (err != ESP_OK) {
    Serial.printf("[Device/BTT Panda Touch] RGB panel failed: %s\n",
                  esp_err_to_name(err));
    return false;
  }
  err = esp_lcd_panel_reset(g_panel);
  if (err != ESP_OK) return false;
  err = esp_lcd_panel_init(g_panel);
  if (err != ESP_OK) {
    Serial.printf("[Device/BTT Panda Touch] RGB panel init failed: %s\n",
                  esp_err_to_name(err));
    return false;
  }
  // The panel keeps a single framebuffer: the bounce buffer serves the DMA.
  // Asking for more than the driver allocated writes out of bounds.
  void* fb0 = nullptr;
  err = esp_lcd_rgb_panel_get_frame_buffer(g_panel, 1, &fb0, nullptr);
  if (err != ESP_OK || fb0 == nullptr) {
    Serial.printf("[Device/BTT Panda Touch] No framebuffer: %s\n",
                  esp_err_to_name(err));
    return false;
  }
  g_framebuffers[0] = static_cast<uint16_t*>(fb0);
  g_framebuffers[1] = nullptr;
  g_displayedBuffer = 0;
  // The panel scans the first buffer, and the UI is drawn into that same one.
  g_framebuffer = g_framebuffers[0];
  memset(g_framebuffers[0], 0,
         static_cast<size_t>(kScreenWidth) * kScreenHeight * sizeof(uint16_t));
  syncRegion(0, 0, kScreenWidth, kScreenHeight);
  g_display_ready = true;
  Serial.printf("[Device/BTT Panda Touch] RGB panel up, %dx%d, pclk=%u MHz\n",
                kScreenWidth, kScreenHeight,
                static_cast<unsigned>(kPanelPclkHz / 1000000U));
  return true;
}

void ensureStorageLayout() {
  if (!g_littlefs_ready) return;
  LittleFS.mkdir("/_tile_grids");
  LittleFS.mkdir("/_tile_links");
}

}  // namespace

bool init() {
  Serial.println("[Device/BTT Panda Touch] Initialising board...");
  if (!initDisplay()) return false;
  backlightInit();
  if (!initTouch()) {
    Serial.println("[Device/BTT Panda Touch] Touch unavailable; continuing");
  }
  return true;
}

void update() {}

void displayPushPixels(int32_t x, int32_t y, int32_t w, int32_t h,
                       const uint16_t* data) {
  if (!g_display_ready || !g_framebuffer || !data || w <= 0 || h <= 0) return;
  if (x < 0) {
    data += -x;
    w += x;
    x = 0;
  }
  if (y < 0) {
    data += static_cast<size_t>(-y) * w;
    h += y;
    y = 0;
  }
  if (x + w > kScreenWidth) w = kScreenWidth - x;
  if (y + h > kScreenHeight) h = kScreenHeight - y;
  if (w <= 0 || h <= 0) return;

  // Only the flip fits a physically landscape panel.
  const bool flipped = (g_rotation & 0x02) != 0;
  for (int32_t row = 0; row < h; ++row) {
    const uint16_t* src = data + static_cast<size_t>(row) * w;
    const int32_t target_row = flipped ? (y + h - 1 - row) : (y + row);
    uint16_t* dst = g_framebuffer + static_cast<size_t>(target_row) * kScreenWidth + x;
    if (!flipped) {
      memcpy(dst, src, static_cast<size_t>(w) * sizeof(uint16_t));
    } else {
      for (int32_t column = 0; column < w; ++column) dst[w - 1 - column] = src[column];
    }
  }
  syncRegion(x, y, w, h);
}

void displayPushPixelsDMA(int32_t x, int32_t y, int32_t w, int32_t h,
                          const uint16_t* data) {
  displayPushPixels(x, y, w, h, data);
}

bool displayTryFullFramePreview(int32_t x, int32_t y, int32_t w, int32_t h,
                                int32_t source_stride,
                                const uint16_t* data, size_t data_size,
                                bool byte_swap) {
  (void)x;
  (void)y;
  (void)w;
  (void)h;
  (void)source_stride;
  (void)data;
  (void)data_size;
  (void)byte_swap;
  return false;
}

void displayWaitDMA() {}

// The panel scans the framebuffer the UI is drawn into. LVGL only redraws
// the changed bands, so a second buffer would show the state of two frames
// ago everywhere the previous frame did not touch. The panel therefore keeps
// scanning the buffer the UI writes into; a band can tear while it is
// copied, but the picture is never a mix of two frames.
void displayWaitDisplay() {}

bool displayBeginAtomicFrame(const char* reason) {
  // One framebuffer, scanned by the LCD: there is no inactive buffer the UI
  // could be rendered into. The caller blanks the screen for the handover.
  (void)reason;
  return false;
}

void displayFillScreen(uint16_t color) {
  if (!g_display_ready || !g_framebuffer) return;
  for (int32_t row = 0; row < kScreenHeight; ++row) {
    uint16_t* line = g_framebuffer + static_cast<size_t>(row) * kScreenWidth;
    for (int32_t column = 0; column < kScreenWidth; ++column) line[column] = color;
  }
  syncRegion(0, 0, kScreenWidth, kScreenHeight);
}

void displaySetRotation(uint8_t rotation) {
  g_rotation = rotation & 0x03;
  if (g_rotation & 0x01) {
    // A quarter turn does not fit the landscape panel; keep the flip.
    g_rotation &= 0x02;
  }
}

void setBrightness(uint8_t value) {
  applyBrightness(value);
}

uint8_t getBrightness() {
  return g_brightness;
}

bool getTouch(int16_t& x, int16_t& y) {
  if (!g_touch_ready && !initTouch()) return false;

  const auto return_held_point = [&]() {
    if (!g_touch_active) return false;
    x = g_touch_x;
    y = g_touch_y;
    return true;
  };
  const auto return_held_or_fail_safe = [&]() {
    if (g_touch_error_streak < UINT8_MAX) ++g_touch_error_streak;
    if (!g_touch_active || g_touch_error_streak < kTouchErrorReleaseThreshold) {
      return return_held_point();
    }
    g_touch_active = false;
    g_touch_error_streak = 0;
    return false;
  };

  uint8_t status = 0;
  if (!readTouchRegisters(kTouchStatusRegister, &status, 1)) {
    return return_held_or_fail_safe();
  }
  g_touch_error_streak = 0;
  if ((status & 0x80) == 0) {
    // Bit 7 marks a new coordinate frame, not a finger on the glass.
    return return_held_point();
  }

  const uint8_t points = status & 0x0F;
  if (points == 0) {
    writeTouchRegister(kTouchStatusRegister, 0);
    g_touch_active = false;
    return false;
  }
  if (points > 5) {
    writeTouchRegister(kTouchStatusRegister, 0);
    return return_held_or_fail_safe();
  }

  uint8_t point[8] = {0};
  const bool read_ok =
      readTouchRegisters(kTouchPointRegister, point, sizeof(point));
  writeTouchRegister(kTouchStatusRegister, 0);
  if (!read_ok) {
    return return_held_or_fail_safe();
  }

  const uint16_t raw_x = static_cast<uint16_t>(point[0] | (point[1] << 8));
  const uint16_t raw_y = static_cast<uint16_t>(point[2] | (point[3] << 8));
  if (raw_x >= kScreenWidth || raw_y >= kScreenHeight) {
    return return_held_or_fail_safe();
  }

  int16_t mapped_x = static_cast<int16_t>(raw_x);
  int16_t mapped_y = static_cast<int16_t>(raw_y);
  if (g_rotation & 0x02) {
    mapped_x = static_cast<int16_t>(kScreenWidth - 1 - mapped_x);
    mapped_y = static_cast<int16_t>(kScreenHeight - 1 - mapped_y);
  }
  g_touch_x = mapped_x;
  g_touch_y = mapped_y;
  g_touch_active = true;
  x = mapped_x;
  y = mapped_y;
  return true;
}

void displaySleep() {
  applyBrightness(0);
}

void displayWake() {
  applyBrightness(g_brightness ? g_brightness : 160);
}

void displayWakeDark() {
  applyBrightness(0);
}

void displayPowerSaveOn() {
  displaySleep();
}

void displayPowerSaveOff() {
  displayWake();
}


void prepareForRestart() {
  applyBrightness(0);
  displayFillScreen(0x0000);
  delay(20);
}

bool initSDCard() {
  // This board has no microSD slot.
  return false;
}

bool storageReady() {
  return g_littlefs_ready;
}

fs::FS& storageFS() {
  return LittleFS;
}

void storageWriteBegin() {
  if (g_storage_write_depth < UINT16_MAX) ++g_storage_write_depth;
  if (g_storage_write_depth != 1) return;
  // The main-flash write stops the PSRAM cache for a moment. Hide the short
  // stall behind a blanked backlight and resynchronise the RGB stream
  // afterwards.
  if (!g_display_ready) return;
  const bool blackout = g_backlight_ready && g_applied_brightness != 0;
  if (blackout) {
    g_storage_blackout_active = true;
    g_storage_restore_brightness = g_applied_brightness;
    applyBrightness(0);
    delay(20);
  }
}

void storageWriteEnd() {
  if (g_storage_write_depth == 0) return;
  --g_storage_write_depth;
  if (g_storage_write_depth != 0) return;

  const bool restart_required = g_storage_blackout_active;
  const uint8_t restore_brightness = g_storage_restore_brightness;
  g_storage_blackout_active = false;
  g_storage_restore_brightness = 0;

  if (restart_required && g_panel) {
    esp_lcd_rgb_panel_restart(g_panel);
  }
  if (restart_required) applyBrightness(restore_brightness);
}

bool sdReady() {
  return false;
}

fs::FS& sdFS() {
  return LittleFS;
}

bool suspendSDCardForNetworkTransition() {
  return false;
}

bool resumeSDCardAfterNetworkTransition() {
  return false;
}

bool initLittleFS() {
  if (g_littlefs_ready) return true;
  if (!LittleFS.begin(true, "/littlefs", 10, "spiffs")) {
    Serial.println("[Device/BTT Panda Touch] LittleFS mount failed");
    return false;
  }
  g_littlefs_ready = true;
  ensureStorageLayout();
  Serial.printf("[Device/BTT Panda Touch] LittleFS ready, total=%u, used=%u\n",
                static_cast<unsigned>(LittleFS.totalBytes()),
                static_cast<unsigned>(LittleFS.usedBytes()));
  return true;
}

void migrateStorageFromSD() {
  if (!initLittleFS() || LittleFS.exists("/_migrated")) return;
  ensureStorageLayout();
  File flag = LittleFS.open("/_migrated", FILE_WRITE);
  if (flag) {
    flag.print("1");
    flag.close();
  }
}

}  // namespace DeviceBTTPandaTouch

#endif  // defined(DEVICE_BTT_PANDA_TOUCH)
