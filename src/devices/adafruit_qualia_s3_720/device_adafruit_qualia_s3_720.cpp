#include "src/devices/adafruit_qualia_s3_720/device_adafruit_qualia_s3_720.h"
#include "src/devices/device_select.h"

#if defined(DEVICE_ADAFRUIT_QUALIA_S3_720)

#include <Arduino.h>
#include <Arduino_GFX_Library.h>

#include <LittleFS.h>
#include <WiFi.h>
#include <Wire.h>
#include <driver/gpio.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>
#include <esp_phy_init.h>
#include <esp_private/periph_ctrl.h>
#include <esp32s3/rom/cache.h>
#include <hal/lcd_ll.h>
#include <soc/periph_defs.h>

#include <algorithm>
#include <cstring>
#include <iterator>

namespace {

constexpr int16_t kPanelSize = 720;

// RGB-565 data bus on the Qualia's 40-pin FPC. The panel is RGB-666; the 16
// LCD_CAM lines land on its upper bits. Pins are fixed by the board.
constexpr int8_t kPanelDe = 2;
constexpr int8_t kPanelVsync = 42;
constexpr int8_t kPanelHsync = 41;
constexpr int8_t kPanelPclk = 1;

constexpr int8_t kPanelB0 = 40;
constexpr int8_t kPanelB1 = 39;
constexpr int8_t kPanelB2 = 38;
constexpr int8_t kPanelB3 = 0;
constexpr int8_t kPanelB4 = 45;

constexpr int8_t kPanelG0 = 48;
constexpr int8_t kPanelG1 = 47;
constexpr int8_t kPanelG2 = 21;
constexpr int8_t kPanelG3 = 14;
constexpr int8_t kPanelG4 = 13;
constexpr int8_t kPanelG5 = 12;

constexpr int8_t kPanelR0 = 11;
constexpr int8_t kPanelR1 = 10;
constexpr int8_t kPanelR2 = 9;
constexpr int8_t kPanelR3 = 46;
constexpr int8_t kPanelR4 = 3;

// Shared I2C bus for the TCA9554 expander and the FT6336 touch controller.
constexpr int8_t kI2cSda = 8;
constexpr int8_t kI2cScl = 18;
constexpr uint32_t kI2cFrequency = 400000;

// TCA9554 expander. The panel reset, the unused panel config SPI lines and
// the backlight enable are expander outputs, not GPIOs.
constexpr uint8_t kExpanderAddress = 0x3F;
constexpr uint8_t kExpanderRegOutput = 0x01;
constexpr uint8_t kExpanderRegConfig = 0x03;
// Direction: bits 0/1/2/4/7 are outputs (TFT SCK, CS, RESET, backlight,
// TFT MOSI); bit 3 touch IRQ and bits 5/6 user buttons stay inputs. This is
// Adafruit's CircuitPython Qualia bus init (0x78) with the backlight bit also
// driven, so the firmware can switch the lamp.
constexpr uint8_t kExpanderConfig = 0x68;
// Output shadow at reset, matching CircuitPython's default. The backlight bit
// is cleared here so the panel stays dark until the first explicit wake.
constexpr uint8_t kExpanderInitialOutput = 0xED;
constexpr uint8_t kExpanderBitTftCs = 1;
constexpr uint8_t kExpanderBitTftReset = 2;
constexpr uint8_t kExpanderBitBacklight = 4;

// FT6336 capacitive touch. Adafruit's square40 panel definition moves it from
// FocalTech's usual 0x38 to 0x48.
constexpr uint8_t kTouchAddress = 0x48;
constexpr uint8_t kTouchRegStatus = 0x02;
constexpr uint8_t kTouchRegChipId = 0xA3;
constexpr uint8_t kTouchRegFirmware = 0xA6;
constexpr uint8_t kTouchErrorReleaseThreshold = 16;
constexpr uint32_t kTouchRetryIntervalMs = 2000;

constexpr uint32_t kExpectedFlashBytes = 16U * 1024U * 1024U;
constexpr uint32_t kExpectedPsramBytes = 8U * 1024U * 1024U;

// TL040HDS20 timings from Adafruit's CircuitPython square40 definition,
// which runs on this exact board and panel.
//
// 12 MHz rather than 16: without panel frame memory, GDMA reads the PSRAM
// framebuffer continuously (2 MB/s per MHz), and at 16 MHz concurrent full
// redraws of a 720x720 frame shifted the image sideways on this board. 12 MHz
// gives about 19.5 Hz and leaves PSRAM headroom for the CPU.
constexpr uint32_t kRgbPclkHz = 12000000;
constexpr uint32_t kRgbHsyncPulseWidth = 2;
constexpr uint32_t kRgbHsyncBackPorch = 44;
constexpr uint32_t kRgbHsyncFrontPorch = 46;
constexpr uint32_t kRgbVsyncPulseWidth = 2;
constexpr uint32_t kRgbVsyncBackPorch = 18;
constexpr uint32_t kRgbVsyncFrontPorch = 16;
// square40 sets pclk_active_high False: pixels latch on the falling edge.
constexpr bool kRgbPclkActiveNeg = true;
constexpr uint32_t kRgbHorizontalTotal =
    kPanelSize + kRgbHsyncPulseWidth + kRgbHsyncBackPorch + kRgbHsyncFrontPorch;
constexpr uint32_t kRgbVerticalTotal =
    kPanelSize + kRgbVsyncPulseWidth + kRgbVsyncBackPorch + kRgbVsyncFrontPorch;
constexpr uint32_t kRgbFramePeriodMs =
    ((kRgbHorizontalTotal * kRgbVerticalTotal * 1000U) + kRgbPclkHz - 1U) /
    kRgbPclkHz;

// About 5 mA instead of the 20 mA default on the fast-switching RGB lines.
// On this board the default drive desensed the nearby 2.4 GHz front end
// (HTTP requests took tens of seconds); the weakest drive restored normal
// Wi-Fi latency with no visible panel change. HSYNC/VSYNC keep full drive.
constexpr gpio_drive_cap_t kRgbDataDrive = GPIO_DRIVE_CAP_0;

#if defined(CONFIG_LCD_RGB_RESTART_IN_VSYNC) && \
    CONFIG_LCD_RGB_RESTART_IN_VSYNC
constexpr bool kRestartInVsync = true;
#else
constexpr bool kRestartInVsync = false;
#endif

void quietenRgbBus() {
  const int8_t pins[] = {
      kPanelB0, kPanelB1, kPanelB2, kPanelB3, kPanelB4,
      kPanelG0, kPanelG1, kPanelG2, kPanelG3, kPanelG4, kPanelG5,
      kPanelR0, kPanelR1, kPanelR2, kPanelR3, kPanelR4,
      kPanelPclk, kPanelDe};
  for (const int8_t pin : pins) {
    gpio_set_drive_capability(static_cast<gpio_num_t>(pin), kRgbDataDrive);
  }
}

class QualiaAtomicRgbDisplay final : public Arduino_RGB_Display {
 public:
  explicit QualiaAtomicRgbDisplay(uint8_t rotation)
      : Arduino_RGB_Display(kPanelSize, kPanelSize, nullptr, rotation, true,
                            nullptr, GFX_NOT_DEFINED, nullptr, 0) {}

  bool begin(int32_t speed = GFX_NOT_DEFINED) override {
    (void)speed;
    // A soft restart leaves LCD_CAM/GDMA running. A new panel would then
    // latch onto the current VSYNC phase and keep the image shifted until a
    // power cycle, so start from the same clean peripheral state as a cold
    // boot.
    periph_module_reset(PERIPH_LCD_CAM_MODULE);

    esp_lcd_rgb_panel_config_t config{};
    config.clk_src = LCD_CLK_SRC_DEFAULT;
    config.timings.pclk_hz = kRgbPclkHz;
    config.timings.h_res = kPanelSize;
    config.timings.v_res = kPanelSize;
    config.timings.hsync_pulse_width = kRgbHsyncPulseWidth;
    config.timings.hsync_back_porch = kRgbHsyncBackPorch;
    config.timings.hsync_front_porch = kRgbHsyncFrontPorch;
    config.timings.vsync_pulse_width = kRgbVsyncPulseWidth;
    config.timings.vsync_back_porch = kRgbVsyncBackPorch;
    config.timings.vsync_front_porch = kRgbVsyncFrontPorch;
    config.timings.flags.hsync_idle_low = 0;
    config.timings.flags.vsync_idle_low = 0;
    config.timings.flags.de_idle_high = 0;
    config.timings.flags.pclk_active_neg = kRgbPclkActiveNeg ? 1 : 0;
    config.timings.flags.pclk_idle_high = 0;
    config.data_width = 16;
    config.bits_per_pixel = 16;
    config.num_fbs = 2;
    config.bounce_buffer_size_px = 0;
    config.sram_trans_align = 8;
    config.psram_trans_align = 64;
    config.hsync_gpio_num = kPanelHsync;
    config.vsync_gpio_num = kPanelVsync;
    config.de_gpio_num = kPanelDe;
    config.pclk_gpio_num = kPanelPclk;
    config.disp_gpio_num = GPIO_NUM_NC;
    const int data_pins[16] = {
        kPanelB0, kPanelB1, kPanelB2, kPanelB3, kPanelB4,
        kPanelG0, kPanelG1, kPanelG2, kPanelG3, kPanelG4, kPanelG5,
        kPanelR0, kPanelR1, kPanelR2, kPanelR3, kPanelR4};
    std::copy(std::begin(data_pins), std::end(data_pins),
              config.data_gpio_nums);
    config.flags.disp_active_low = false;
    config.flags.refresh_on_demand = false;
    config.flags.fb_in_psram = true;
    config.flags.double_fb = true;
    config.flags.no_fb = false;
    config.flags.bb_invalidate_cache = false;

    esp_err_t err = esp_lcd_new_rgb_panel(&config, &panel_handle_);
    if (err == ESP_OK) {
      esp_lcd_rgb_panel_event_callbacks_t callbacks{};
      callbacks.on_vsync = onVsync;
      callbacks.on_frame_buf_complete = onFrameComplete;
      err = esp_lcd_rgb_panel_register_event_callbacks(
          panel_handle_, &callbacks, this);
    }
    if (err == ESP_OK) err = esp_lcd_panel_reset(panel_handle_);
    if (err == ESP_OK) err = esp_lcd_panel_init(panel_handle_);
    if (err == ESP_OK) {
      // esp_lcd configures the pins itself; weaken them afterwards.
      quietenRgbBus();
      maskVsyncInterrupt();
    }
    if (err == ESP_OK) {
      err = esp_lcd_rgb_panel_get_frame_buffer(
          panel_handle_, 2, reinterpret_cast<void**>(&framebuffers_[0]),
          reinterpret_cast<void**>(&framebuffers_[1]));
    }
    if (err != ESP_OK || !framebuffers_[0] || !framebuffers_[1]) {
      Serial.printf(
          "[Display/S3] Double framebuffer init failed: %s (0x%X)\n",
          esp_err_to_name(err), static_cast<unsigned>(err));
      return false;
    }

    active_index_ = 0;
    pending_index_ = 0;
    atomic_pending_ = false;
    canonical_fb0_valid_ = true;
    _framebuffer = framebuffers_[0];

    return true;
  }

  bool beginAtomicFrame(const char* reason) {
    if (!panel_handle_ || !framebuffers_[0] || !framebuffers_[1]) {
      return false;
    }
    if (storage_transition_) return false;
    if (atomic_pending_) return true;

    pending_index_ = active_index_ ^ 1U;
    if (pending_index_ == 0) canonical_fb0_valid_ = false;
    _framebuffer = framebuffers_[pending_index_];
    atomic_pending_ = true;
    atomic_started_ms_ = millis();
    atomic_reason_ = reason ? reason : "unknown";
    return true;
  }

  bool commitAtomicFrame() {
    if (!atomic_pending_ || !panel_handle_) return false;

    flush(true);
    if (pending_index_ == 0) canonical_fb0_valid_ = true;
    const uint32_t eof_start = frame_complete_count_;
    const esp_err_t err = esp_lcd_panel_draw_bitmap(
        panel_handle_, 0, 0, _fb_width, _fb_height,
        framebuffers_[pending_index_]);
    const bool presented =
        err == ESP_OK && waitForFrameCompletions(eof_start, 3,
                                                 kRgbFramePeriodMs * 5U + 20U);
    if (err == ESP_OK) {
      active_index_ = pending_index_;
      canonical_fb0_valid_ = active_index_ == 0;
    }
    _framebuffer = framebuffers_[active_index_];
    atomic_pending_ = false;
    atomic_reason_ = "none";
    atomic_started_ms_ = 0;
    return err == ESP_OK && presented;
  }

  void service() {
    if (!atomic_pending_ || atomic_started_ms_ == 0 ||
        millis() - atomic_started_ms_ < 15000U) {
      return;
    }
    _framebuffer = framebuffers_[active_index_];
    atomic_pending_ = false;
    atomic_started_ms_ = 0;
    Serial.printf(
        "[Display/S3] Atomic redraw timeout, keeping framebuffer %u\n",
        static_cast<unsigned>(active_index_));
    atomic_reason_ = "none";
  }

  bool canonicalizeForStorage() {
    if (!panel_handle_) return false;
    storage_transition_ = true;
    if (atomic_pending_) {
      commitAtomicFrame();
    }
    if (active_index_ == 0) {
      _framebuffer = framebuffers_[0];
      canonical_fb0_valid_ = true;
      return true;
    }

    memcpy(framebuffers_[0], framebuffers_[active_index_], _framebuffer_size);
    Cache_WriteBack_Addr(
        reinterpret_cast<uint32_t>(framebuffers_[0]), _framebuffer_size);
    canonical_fb0_valid_ = true;
    const uint32_t eof_start = frame_complete_count_;
    const esp_err_t err = esp_lcd_panel_draw_bitmap(
        panel_handle_, 0, 0, _fb_width, _fb_height, framebuffers_[0]);
    if (err == ESP_OK) {
      waitForFrameCompletions(eof_start, 3, kRgbFramePeriodMs * 5U + 20U);
    }
    if (err == ESP_OK) {
      active_index_ = 0;
    }
    _framebuffer = framebuffers_[active_index_];
    return canonical_fb0_valid_;
  }

  esp_err_t restartAfterStorage(uint32_t& wait_ms) {
    wait_ms = 0;
    if (!panel_handle_ || !canonical_fb0_valid_) {
      storage_transition_ = false;
      return ESP_ERR_INVALID_STATE;
    }

    restart_vsync_seen_ = false;
    restart_one_shot_armed_ = true;
    const uint32_t started_ms = millis();
    esp_err_t err = esp_lcd_rgb_panel_restart(panel_handle_);
    if (err == ESP_OK) {
      enableVsyncInterruptOneShot();
      while (!restart_vsync_seen_ &&
             millis() - started_ms < kRgbFramePeriodMs * 3U + 20U) {
        delay(1);
      }
      if (!restart_vsync_seen_) {
        err = ESP_ERR_TIMEOUT;
      } else {
        const uint32_t eof_start = restart_eof_baseline_;
        if (!waitForFrameCompletions(eof_start, 2,
                                     kRgbFramePeriodMs * 4U + 20U)) {
          err = ESP_ERR_TIMEOUT;
        }
      }
    }
    maskVsyncInterrupt();
    restart_one_shot_armed_ = false;
    if (restart_vsync_seen_) {
      active_index_ = 0;
      _framebuffer = framebuffers_[0];
    }
    wait_ms = millis() - started_ms;
    storage_transition_ = false;
    return err;
  }

 private:
  static bool IRAM_ATTR onFrameComplete(
      esp_lcd_panel_handle_t panel,
      const esp_lcd_rgb_panel_event_data_t* event_data, void* user_ctx) {
    (void)panel;
    (void)event_data;
    auto* self = static_cast<QualiaAtomicRgbDisplay*>(user_ctx);
    if (self) ++self->frame_complete_count_;
    return false;
  }

  static bool IRAM_ATTR onVsync(
      esp_lcd_panel_handle_t panel,
      const esp_lcd_rgb_panel_event_data_t* event_data, void* user_ctx) {
    (void)panel;
    (void)event_data;
    auto* self = static_cast<QualiaAtomicRgbDisplay*>(user_ctx);
    if (!self || !self->restart_one_shot_armed_) return false;

    PERIPH_RCC_ATOMIC() {
      lcd_ll_enable_interrupt(&LCD_CAM, LCD_LL_EVENT_RGB, false);
    }
    self->restart_one_shot_armed_ = false;
    self->restart_eof_baseline_ = self->frame_complete_count_;
    self->restart_vsync_seen_ = true;
    return false;
  }

  static void maskVsyncInterrupt() {
    PERIPH_RCC_ATOMIC() {
      lcd_ll_enable_interrupt(&LCD_CAM, LCD_LL_EVENT_RGB, false);
      lcd_ll_clear_interrupt_status(&LCD_CAM, LCD_LL_EVENT_RGB);
    }
  }

  static void enableVsyncInterruptOneShot() {
    PERIPH_RCC_ATOMIC() {
      lcd_ll_clear_interrupt_status(&LCD_CAM, LCD_LL_EVENT_RGB);
      lcd_ll_enable_interrupt(&LCD_CAM, LCD_LL_EVENT_RGB, true);
    }
  }

  bool waitForFrameCompletions(uint32_t start, uint32_t count,
                               uint32_t timeout_ms) const {
    const uint32_t started_ms = millis();
    while (static_cast<uint32_t>(frame_complete_count_ - start) < count &&
           millis() - started_ms < timeout_ms) {
      delay(1);
    }
    return static_cast<uint32_t>(frame_complete_count_ - start) >= count;
  }

  esp_lcd_panel_handle_t panel_handle_ = nullptr;
  uint16_t* framebuffers_[2] = {nullptr, nullptr};
  uint8_t active_index_ = 0;
  uint8_t pending_index_ = 0;
  bool atomic_pending_ = false;
  bool storage_transition_ = false;
  bool canonical_fb0_valid_ = true;
  uint32_t atomic_started_ms_ = 0;
  const char* atomic_reason_ = "none";
  volatile uint32_t frame_complete_count_ = 0;
  volatile bool restart_one_shot_armed_ = false;
  volatile bool restart_vsync_seen_ = false;
  volatile uint32_t restart_eof_baseline_ = 0;
};

QualiaAtomicRgbDisplay* g_gfx = nullptr;

bool g_display_ready = false;
bool g_expander_ready = false;
bool g_touch_ready = false;
bool g_littlefs_ready = false;
uint8_t g_expander_output = kExpanderInitialOutput;
uint8_t g_brightness = 0;
uint8_t g_applied_brightness = 0;
uint8_t g_rotation = DeviceAdafruitQualiaS3720::kProfile.rotation_default;
uint32_t g_touch_last_probe_ms = 0;
bool g_touch_probed = false;
uint16_t g_storage_write_depth = 0;
bool g_storage_blackout_active = false;
bool g_storage_restart_required = false;
uint8_t g_storage_restore_brightness = 0;
bool g_touch_active = false;
int16_t g_touch_last_x = 0;
int16_t g_touch_last_y = 0;
uint8_t g_touch_error_streak = 0;

void ensureStorageLayout() {
  if (!g_littlefs_ready) return;
  LittleFS.mkdir("/_tile_grids");
  LittleFS.mkdir("/_tile_links");
  LittleFS.mkdir("/icons");
}

void recoverI2CBus(int sda, int scl) {
  Wire.end();
  delay(2);
  pinMode(sda, INPUT_PULLUP);
  pinMode(scl, OUTPUT);
  digitalWrite(scl, HIGH);
  delayMicroseconds(10);

  for (int i = 0; i < 9 && digitalRead(sda) == LOW; ++i) {
    digitalWrite(scl, LOW);
    delayMicroseconds(10);
    digitalWrite(scl, HIGH);
    delayMicroseconds(10);
  }

  digitalWrite(sda, LOW);
  pinMode(sda, OUTPUT);
  delayMicroseconds(10);
  digitalWrite(scl, HIGH);
  delayMicroseconds(10);
  digitalWrite(sda, HIGH);
  delayMicroseconds(10);

  pinMode(sda, INPUT_PULLUP);
  pinMode(scl, INPUT_PULLUP);
  delay(2);
}

bool writeI2cRegister(uint8_t address, uint8_t reg, uint8_t value,
                      uint8_t retries = 5) {
  for (uint8_t attempt = 0; attempt < retries; ++attempt) {
    Wire.beginTransmission(address);
    Wire.write(reg);
    Wire.write(value);
    if (Wire.endTransmission() == 0) return true;
    delay(10 + attempt * 10);
  }
  return false;
}

bool readI2cRegisters(uint8_t address, uint8_t reg, uint8_t* data,
                      size_t len) {
  if (!data || len == 0 || len > 32) return false;
  Wire.beginTransmission(address);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  const size_t received =
      Wire.requestFrom(static_cast<int>(address), static_cast<int>(len));
  if (received != len) {
    while (Wire.available()) Wire.read();
    return false;
  }
  for (size_t i = 0; i < len; ++i) {
    data[i] = static_cast<uint8_t>(Wire.read());
  }
  return true;
}

bool writeExpanderOutput(uint8_t value) {
  g_expander_output = value;
  return writeI2cRegister(kExpanderAddress, kExpanderRegOutput, value);
}

void setExpanderBit(uint8_t bit, bool high) {
  const uint8_t mask = static_cast<uint8_t>(1U << bit);
  writeExpanderOutput(high ? static_cast<uint8_t>(g_expander_output | mask)
                           : static_cast<uint8_t>(g_expander_output & ~mask));
}

bool initExpander() {
  if (g_expander_ready) return true;

  recoverI2CBus(kI2cSda, kI2cScl);
  Wire.begin(kI2cSda, kI2cScl, kI2cFrequency);
  delay(20);

  Wire.beginTransmission(kExpanderAddress);
  if (Wire.endTransmission() != 0) {
    Serial.println(
        "[Device/Adafruit Qualia S3 720] TCA9554 expander not found at 0x3F");
    return false;
  }

  // Known output levels first, then directions, so no line is briefly
  // driven to a stale port value.
  if (!writeExpanderOutput(kExpanderInitialOutput) ||
      !writeI2cRegister(kExpanderAddress, kExpanderRegConfig,
                        kExpanderConfig)) {
    Serial.println(
        "[Device/Adafruit Qualia S3 720] TCA9554 expander configuration failed");
    return false;
  }

  // The TL040HDS20 needs no register initialization: this reset pulse is its
  // whole power-on sequence. The unused config SPI chip select stays high.
  setExpanderBit(kExpanderBitTftCs, true);
  setExpanderBit(kExpanderBitTftReset, true);
  delay(10);
  setExpanderBit(kExpanderBitTftReset, false);
  delay(20);
  setExpanderBit(kExpanderBitTftReset, true);
  delay(120);

  g_expander_ready = true;
  Serial.println(
      "[Device/Adafruit Qualia S3 720] TCA9554 ready, panel reset released");
  return true;
}

void applyBrightness(uint8_t value, bool remember = true) {
  if (remember) g_brightness = value;
  if (!g_expander_ready) return;
  // On/off only: the Qualia routes the backlight enable through the
  // expander, which has no PWM output.
  const bool on = value != 0;
  if (on != (g_applied_brightness != 0)) {
    setExpanderBit(kExpanderBitBacklight, on);
  }
  g_applied_brightness = value;
}

bool initTouch() {
  if (g_touch_ready) return true;
  const uint32_t now = millis();
  if (g_touch_probed && now - g_touch_last_probe_ms < kTouchRetryIntervalMs) {
    return false;
  }
  const bool first_probe = !g_touch_probed;
  g_touch_probed = true;
  g_touch_last_probe_ms = now;

  uint8_t chip_id = 0;
  if (!readI2cRegisters(kTouchAddress, kTouchRegChipId, &chip_id, 1)) {
    if (first_probe) {
      Serial.println(
          "[Device/Adafruit Qualia S3 720] FT6336 not found at 0x48");
    }
    return false;
  }
  uint8_t firmware = 0;
  readI2cRegisters(kTouchAddress, kTouchRegFirmware, &firmware, 1);
  Serial.printf(
      "[Device/Adafruit Qualia S3 720] FT6336 at 0x48, chip=0x%02X, fw=0x%02X\n",
      chip_id, firmware);

  g_touch_active = false;
  g_touch_error_streak = 0;
  g_touch_ready = true;
  return true;
}

bool initDisplay() {
  if (g_display_ready) return true;

  g_gfx = new QualiaAtomicRgbDisplay(g_rotation);
  if (!g_gfx || !g_gfx->begin()) {
    Serial.println(
        "[Device/Adafruit Qualia S3 720] TL040HDS20 RGB display init failed");
    return false;
  }

  g_gfx->fillScreen(0x0000);
  g_display_ready = true;
  Serial.printf(
      "[Device/Adafruit Qualia S3 720] Display ready, PCLK=%u MHz, "
      "frame=%u ms, VSYNC-restart=%u, PSRAM free=%u KB\n",
      static_cast<unsigned>(kRgbPclkHz / 1000000),
      static_cast<unsigned>(kRgbFramePeriodMs),
      kRestartInVsync ? 1U : 0U,
      static_cast<unsigned>(
          heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
  return true;
}

void mapTouch(uint16_t raw_x, uint16_t raw_y, int16_t& x, int16_t& y) {
  constexpr int16_t kMax = kPanelSize - 1;
  switch (g_rotation & 0x03) {
    case 1:
      x = static_cast<int16_t>(raw_y);
      y = kMax - static_cast<int16_t>(raw_x);
      break;
    case 2:
      x = kMax - static_cast<int16_t>(raw_x);
      y = kMax - static_cast<int16_t>(raw_y);
      break;
    case 3:
      x = kMax - static_cast<int16_t>(raw_y);
      y = static_cast<int16_t>(raw_x);
      break;
    default:
      x = static_cast<int16_t>(raw_x);
      y = static_cast<int16_t>(raw_y);
      break;
  }
  x = std::max<int16_t>(0, std::min<int16_t>(kMax, x));
  y = std::max<int16_t>(0, std::min<int16_t>(kMax, y));
}

}  // namespace

bool DeviceAdafruitQualiaS3720::init() {
  Serial.println("[Device/Adafruit Qualia S3 720] Initialising board...");

  if (!psramFound()) {
    Serial.println(
        "[Device/Adafruit Qualia S3 720] ERROR: octal PSRAM not detected");
    return false;
  }
  const uint32_t flash_bytes = ESP.getFlashChipSize();
  const uint32_t psram_bytes = ESP.getPsramSize();
  if (flash_bytes != kExpectedFlashBytes ||
      psram_bytes != kExpectedPsramBytes) {
    Serial.printf(
        "[Device/Adafruit Qualia S3 720] ERROR: expected N16R8, "
        "detected flash=%u MB, PSRAM=%u MB\n",
        static_cast<unsigned>(flash_bytes / (1024U * 1024U)),
        static_cast<unsigned>(psram_bytes / (1024U * 1024U)));
    return false;
  }
  Serial.printf(
      "[Device/Adafruit Qualia S3 720] Flash=%u MB, PSRAM=%u MB\n",
      static_cast<unsigned>(flash_bytes / (1024U * 1024U)),
      static_cast<unsigned>(psram_bytes / (1024U * 1024U)));

  if (!initExpander()) return false;
  applyBrightness(0, false);

  if (!initLittleFS()) return false;

  auto* phy_calibration = static_cast<esp_phy_calibration_data_t*>(
      heap_caps_malloc(sizeof(esp_phy_calibration_data_t),
                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  esp_err_t phy_load_err = phy_calibration
                               ? esp_phy_load_cal_data_from_nvs(phy_calibration)
                               : ESP_ERR_NO_MEM;
  if (phy_load_err != ESP_OK) {
    WiFi.persistent(false);
    const bool wifi_started = WiFi.mode(WIFI_STA);
    const bool wifi_stopped = wifi_started && WiFi.mode(WIFI_OFF);
    if (wifi_started && wifi_stopped && phy_calibration) {
      phy_load_err = esp_phy_load_cal_data_from_nvs(phy_calibration);
    }
  }
  if (phy_calibration) heap_caps_free(phy_calibration);

  if (!initDisplay()) return false;

  // FocalTech controllers ignore I2C briefly after the panel reset.
  delay(100);
  if (!initTouch()) {
    Serial.println(
        "[Device/Adafruit Qualia S3 720] Touch unavailable; continuing");
  }
  return true;
}

void DeviceAdafruitQualiaS3720::update() {
  if (g_gfx) g_gfx->service();
}

void DeviceAdafruitQualiaS3720::displayPushPixels(
    int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* data) {
  if (!g_display_ready || !g_gfx || !data || w <= 0 || h <= 0) return;
  g_gfx->draw16bitRGBBitmap(
      static_cast<int16_t>(x), static_cast<int16_t>(y),
      const_cast<uint16_t*>(data), static_cast<int16_t>(w),
      static_cast<int16_t>(h));
}

void DeviceAdafruitQualiaS3720::displayPushPixelsDMA(
    int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* data) {
  displayPushPixels(x, y, w, h, data);
}

bool DeviceAdafruitQualiaS3720::displayTryFullFramePreview(
    int32_t x, int32_t y, int32_t w, int32_t h,
    int32_t source_stride, const uint16_t* data, size_t data_size,
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

bool DeviceAdafruitQualiaS3720::displayBeginAtomicFrame(const char* reason) {
  return g_display_ready && g_gfx && g_gfx->beginAtomicFrame(reason);
}

void DeviceAdafruitQualiaS3720::displayWaitDMA() {}

void DeviceAdafruitQualiaS3720::displayFillScreen(uint16_t color) {
  if (g_display_ready && g_gfx) g_gfx->fillScreen(color);
}

void DeviceAdafruitQualiaS3720::displaySetRotation(uint8_t rotation) {
  g_rotation = rotation & 0x03;
  if (g_display_ready && g_gfx) g_gfx->setRotation(g_rotation);
}

void DeviceAdafruitQualiaS3720::setBrightness(uint8_t value) {
  applyBrightness(value);
}

uint8_t DeviceAdafruitQualiaS3720::getBrightness() {
  return g_brightness;
}

bool DeviceAdafruitQualiaS3720::getTouch(int16_t& x, int16_t& y) {
  if (!g_touch_ready && !initTouch()) return false;

  const auto fail_or_hold = [&]() {
    if (g_touch_error_streak < UINT8_MAX) ++g_touch_error_streak;
    if (!g_touch_active) return false;
    if (g_touch_error_streak >= kTouchErrorReleaseThreshold) {
      g_touch_active = false;
      g_touch_error_streak = 0;
      return false;
    }
    x = g_touch_last_x;
    y = g_touch_last_y;
    return true;
  };

  // TD_STATUS followed by the first point: XH, XL, YH, YL.
  uint8_t data[5] = {};
  if (!readI2cRegisters(kTouchAddress, kTouchRegStatus, data, sizeof(data))) {
    return fail_or_hold();
  }
  const uint8_t points = data[0] & 0x0F;
  if (points == 0) {
    g_touch_active = false;
    g_touch_error_streak = 0;
    return false;
  }
  if (points > 2) {
    return fail_or_hold();
  }

  // Twelve coordinate bits per axis; the top bits of XH/YH carry the event
  // flag and touch ID.
  const uint16_t raw_x =
      static_cast<uint16_t>(((data[1] & 0x0F) << 8) | data[2]);
  const uint16_t raw_y =
      static_cast<uint16_t>(((data[3] & 0x0F) << 8) | data[4]);
  if (raw_x >= kPanelSize || raw_y >= kPanelSize) {
    return fail_or_hold();
  }

  g_touch_error_streak = 0;
  mapTouch(raw_x, raw_y, x, y);
  g_touch_last_x = x;
  g_touch_last_y = y;
  g_touch_active = true;
  return true;
}

void DeviceAdafruitQualiaS3720::displaySleep() {
  applyBrightness(0, false);
}

void DeviceAdafruitQualiaS3720::displayWake() {
  applyBrightness(g_brightness ? g_brightness : 160, false);
}

void DeviceAdafruitQualiaS3720::displayWakeDark() {
  applyBrightness(0, false);
}

void DeviceAdafruitQualiaS3720::displayPowerSaveOn() {
  displaySleep();
}

void DeviceAdafruitQualiaS3720::displayPowerSaveOff() {
  displayWake();
}

void DeviceAdafruitQualiaS3720::displayWaitDisplay() {
  if (g_display_ready && g_gfx) g_gfx->commitAtomicFrame();
}

void DeviceAdafruitQualiaS3720::prepareForRestart() {
  applyBrightness(0, false);
  if (g_display_ready && g_gfx) {
    g_gfx->fillScreen(0x0000);
    g_gfx->flush(true);
  }
  delay(20);
}

bool DeviceAdafruitQualiaS3720::initSDCard() {
  // The Qualia ESP32-S3 has no microSD interface.
  return false;
}

bool DeviceAdafruitQualiaS3720::storageReady() {
  return g_littlefs_ready;
}

fs::FS& DeviceAdafruitQualiaS3720::storageFS() {
  return LittleFS;
}

void DeviceAdafruitQualiaS3720::storageWriteBegin() {
  if (g_storage_write_depth < UINT16_MAX) {
    ++g_storage_write_depth;
  }
  if (g_storage_write_depth != 1) return;
  if (!g_display_ready) return;

  // Without a bounce buffer, GDMA reads the framebuffer from PSRAM, which is
  // unavailable while the flash cache is disabled. Park the scanout on FB0
  // and resynchronize it after the write.
  g_storage_restart_required = true;
  const bool blackout = g_applied_brightness != 0;
  if (blackout) {
    g_storage_blackout_active = true;
    g_storage_restore_brightness = g_applied_brightness;
    applyBrightness(0, false);
    delay(2);
  }

  if (g_gfx && !g_gfx->canonicalizeForStorage()) {
    Serial.println(
        "[Display/S3] Failed to canonicalize framebuffer before flash write");
  }
}

void DeviceAdafruitQualiaS3720::storageWriteEnd() {
  if (g_storage_write_depth == 0) return;
  --g_storage_write_depth;
  if (g_storage_write_depth != 0) return;

  const bool restart_required = g_storage_restart_required;
  const bool restore_backlight = g_storage_blackout_active;
  const uint8_t restore_brightness = g_storage_restore_brightness;
  g_storage_restart_required = false;
  g_storage_blackout_active = false;
  g_storage_restore_brightness = 0;

  if (restart_required) {
    uint32_t restart_wait_ms = 0;
    const esp_err_t restart_result =
        g_gfx ? g_gfx->restartAfterStorage(restart_wait_ms)
              : ESP_ERR_INVALID_STATE;
    if (restart_result != ESP_OK) {
      Serial.printf(
          "[Display/S3] RGB restart after flash write failed: %s (0x%X)\n",
          esp_err_to_name(restart_result),
          static_cast<unsigned>(restart_result));
    }
    (void)restart_wait_ms;
  }

  if (restore_backlight) applyBrightness(restore_brightness, false);
}

bool DeviceAdafruitQualiaS3720::sdReady() {
  return false;
}

fs::FS& DeviceAdafruitQualiaS3720::sdFS() {
  return LittleFS;
}

bool DeviceAdafruitQualiaS3720::suspendSDCardForNetworkTransition() {
  return false;
}

bool DeviceAdafruitQualiaS3720::resumeSDCardAfterNetworkTransition() {
  return false;
}

bool DeviceAdafruitQualiaS3720::initLittleFS() {
  if (g_littlefs_ready) return true;
  if (!LittleFS.begin(true, "/littlefs", 10, "spiffs")) {
    Serial.println("[Device/Adafruit Qualia S3 720] LittleFS mount failed");
    return false;
  }
  g_littlefs_ready = true;
  ensureStorageLayout();
  Serial.printf(
      "[Device/Adafruit Qualia S3 720] LittleFS ready, total=%u, used=%u\n",
      static_cast<unsigned>(LittleFS.totalBytes()),
      static_cast<unsigned>(LittleFS.usedBytes()));
  return true;
}

void DeviceAdafruitQualiaS3720::migrateStorageFromSD() {
  if (!initLittleFS() || LittleFS.exists("/_migrated")) return;

  storageWriteBegin();
  ensureStorageLayout();
  Serial.println(
      "[Storage] SD is not supported by this profile; LittleFS is active");

  File flag = LittleFS.open("/_migrated", FILE_WRITE);
  if (flag) {
    flag.print("1");
    flag.close();
  }
  storageWriteEnd();
}

#endif  // defined(DEVICE_ADAFRUIT_QUALIA_S3_720)
