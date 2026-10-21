#include "src/devices/waveshare_s3_touch_lcd_4/device_waveshare_s3_touch_lcd_4.h"
#include "src/devices/device_select.h"

#if defined(DEVICE_WAVESHARE_S3_TOUCH_LCD_4)

#include <Arduino.h>
#include <Arduino_GFX_Library.h>

#include <LittleFS.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>
#include <esp_phy_init.h>
#include <esp_private/periph_ctrl.h>
#include <esp32s3/rom/cache.h>
#include <hal/lcd_ll.h>

#include <algorithm>
#include <cstring>
#include <iterator>

namespace {

// Panel 3-wire SPI pins (direct GPIO on ESP32-S3-Touch-LCD-4)
constexpr int8_t kPanelSpiCs = 42;
constexpr int8_t kPanelSpiSck = 2;
constexpr int8_t kPanelSpiMosi = 1;

// Panel RGB control and data pins
constexpr int8_t kPanelDe = 40;
constexpr int8_t kPanelVsync = 39;
constexpr int8_t kPanelHsync = 38;
constexpr int8_t kPanelPclk = 41;

constexpr int8_t kPanelR0 = 46;
constexpr int8_t kPanelR1 = 3;
constexpr int8_t kPanelR2 = 8;
constexpr int8_t kPanelR3 = 18;
constexpr int8_t kPanelR4 = 17;

constexpr int8_t kPanelG0 = 14;
constexpr int8_t kPanelG1 = 13;
constexpr int8_t kPanelG2 = 12;
constexpr int8_t kPanelG3 = 11;
constexpr int8_t kPanelG4 = 10;
constexpr int8_t kPanelG5 = 9;

constexpr int8_t kPanelB0 = 5;
constexpr int8_t kPanelB1 = 45;
constexpr int8_t kPanelB2 = 48;
constexpr int8_t kPanelB3 = 47;
constexpr int8_t kPanelB4 = 21;

// Rev 4.0 I2C bus shared by the CH32V003 helper, GT911 touch, and RTC.
constexpr int8_t kI2cSda = 15;
constexpr int8_t kI2cScl = 7;
constexpr uint32_t kI2cFrequency = 400000;

// CH32V003 helper controller registers and pins (used on Rev 4.0)
constexpr uint8_t kCh32Address = 0x24;
constexpr uint8_t kCh32RegDirection = 0x02;
constexpr uint8_t kCh32RegOutput = 0x03;
constexpr uint8_t kCh32RegInput = 0x04;
constexpr uint8_t kCh32RegPwm = 0x05;
constexpr uint8_t kCh32RegAdc = 0x06;
constexpr uint8_t kCh32RegRtcInt = 0x07;

constexpr uint8_t kCh32PinLcdTouchRst = (1 << 1);  // Bit 1: Touch Reset
constexpr uint8_t kCh32PinLcdRst = (1 << 3);       // Bit 3: LCD Reset
constexpr uint8_t kCh32PinSysEn = (1 << 5);        // Bit 5: System / Power Enable
constexpr uint8_t kCh32PinBeeEn = (1 << 6);        // Bit 6: Buzzer Enable
constexpr uint8_t kCh32PinRtcInt = (1 << 7);       // Bit 7: RTC Interrupt

constexpr uint8_t kCh32DirDefault = 0xFF;
constexpr uint8_t kCh32OutReset = 0x00;
constexpr uint8_t kCh32OutDisplayOn =
    kCh32PinSysEn | kCh32PinLcdRst | kCh32PinLcdTouchRst;  // 0x2A

enum class HelperType {
  None,
  CH32V003,
};

// GT911 capacitive touch controller
constexpr uint8_t kTouchAddressPrimary = 0x5D;
constexpr uint8_t kTouchAddressAlternate = 0x14;
constexpr uint16_t kTouchProductIdRegister = 0x8140;
constexpr uint16_t kTouchStatusRegister = 0x814E;
constexpr uint16_t kTouchPointRegister = 0x814F;
constexpr uint8_t kTouchErrorReleaseThreshold = 16;

constexpr uint32_t kExpectedFlashBytes = 16U * 1024U * 1024U;
constexpr uint32_t kExpectedPsramBytes = 8U * 1024U * 1024U;

#if (defined(CONFIG_SPIRAM_XIP_FROM_PSRAM) && CONFIG_SPIRAM_XIP_FROM_PSRAM) || \
    ((defined(CONFIG_SPIRAM_FETCH_INSTRUCTIONS) && CONFIG_SPIRAM_FETCH_INSTRUCTIONS) && \
     (defined(CONFIG_SPIRAM_RODATA) && CONFIG_SPIRAM_RODATA))
#define HOMETILES_WAVESHARE_S3_HAS_PSRAM_XIP 1
#else
#define HOMETILES_WAVESHARE_S3_HAS_PSRAM_XIP 0
#endif

#if defined(CONFIG_ESP32S3_DATA_CACHE_LINE_64B) && \
    CONFIG_ESP32S3_DATA_CACHE_LINE_64B
#define HOMETILES_WAVESHARE_S3_HAS_CACHE_LINE_64B 1
#else
#define HOMETILES_WAVESHARE_S3_HAS_CACHE_LINE_64B 0
#endif

#if HOMETILES_WAVESHARE_S3_HAS_PSRAM_XIP && \
    HOMETILES_WAVESHARE_S3_HAS_CACHE_LINE_64B
#define HOMETILES_WAVESHARE_S3_RGB_MODE_LABEL "xip-bounce10"
#define HOMETILES_WAVESHARE_S3_RGB_BOUNCE_ROWS 10
#else
#define HOMETILES_WAVESHARE_S3_RGB_MODE_LABEL "direct-flash-guard"
#define HOMETILES_WAVESHARE_S3_RGB_BOUNCE_ROWS 0
#endif

// Panel timings matching Waveshare official Arduino 09_LVGL_Widgets and ESP-IDF BSP.
// Using 10 MHz pixel clock and 50-cycle horizontal back porch ensures ample GDMA
// PSRAM bandwidth and FIFO refill margin, preventing horizontal drift/underflow during
// concurrent CPU PSRAM writes on touch events.
constexpr uint32_t kRgbPclkHz = 10000000;
constexpr uint32_t kRgbHsyncPulseWidth = 8;
constexpr uint32_t kRgbHsyncBackPorch = 50;
constexpr uint32_t kRgbHsyncFrontPorch = 10;
constexpr uint32_t kRgbVsyncPulseWidth = 8;
constexpr uint32_t kRgbVsyncBackPorch = 20;
constexpr uint32_t kRgbVsyncFrontPorch = 10;
constexpr size_t kRgbBounceBufferPixels =
    480 * HOMETILES_WAVESHARE_S3_RGB_BOUNCE_ROWS;
constexpr uint32_t kRgbHorizontalTotal =
    480 + kRgbHsyncPulseWidth + kRgbHsyncBackPorch + kRgbHsyncFrontPorch;
constexpr uint32_t kRgbVerticalTotal =
    480 + kRgbVsyncPulseWidth + kRgbVsyncBackPorch + kRgbVsyncFrontPorch;
constexpr uint32_t kRgbFramePeriodMs =
    ((kRgbHorizontalTotal * kRgbVerticalTotal * 1000U) + kRgbPclkHz - 1U) /
    kRgbPclkHz;
constexpr bool kHasPsramXip = HOMETILES_WAVESHARE_S3_HAS_PSRAM_XIP != 0;
constexpr bool kHasCacheLine64 =
    HOMETILES_WAVESHARE_S3_HAS_CACHE_LINE_64B != 0;
constexpr const char* kPanelInitLabel = "waveshare-bsp";

// Byte-for-byte ST7701 command table from Waveshare official BSP.
const uint8_t kPanelInitOperations[] = {
    BEGIN_WRITE,
    WRITE_COMMAND_8, 0x11,
    END_WRITE,
    DELAY, 120,
    BEGIN_WRITE,
    WRITE_COMMAND_8, 0xFF,
    WRITE_BYTES, 5, 0x77, 0x01, 0x00, 0x00, 0x10,
    WRITE_C8_D16, 0xC0, 0x3B, 0x00,
    WRITE_C8_D16, 0xC1, 0x0D, 0x02,
    WRITE_C8_D16, 0xC2, 0x21, 0x08,
    WRITE_C8_D8, 0xCD, 0x08,
    WRITE_COMMAND_8, 0xB0,
    WRITE_BYTES, 16,
    0x00, 0x11, 0x18, 0x0E, 0x11, 0x06, 0x07, 0x08,
    0x07, 0x22, 0x04, 0x12, 0x0F, 0xAA, 0x31, 0x18,
    WRITE_COMMAND_8, 0xB1,
    WRITE_BYTES, 16,
    0x00, 0x11, 0x19, 0x0E, 0x12, 0x07, 0x08, 0x08,
    0x08, 0x22, 0x04, 0x11, 0x11, 0xA9, 0x32, 0x18,
    WRITE_COMMAND_8, 0xFF,
    WRITE_BYTES, 5, 0x77, 0x01, 0x00, 0x00, 0x11,
    WRITE_C8_D8, 0xB0, 0x60,
    WRITE_C8_D8, 0xB1, 0x30,
    WRITE_C8_D8, 0xB2, 0x87,
    WRITE_C8_D8, 0xB3, 0x80,
    WRITE_C8_D8, 0xB5, 0x49,
    WRITE_C8_D8, 0xB7, 0x85,
    WRITE_C8_D8, 0xB8, 0x21,
    WRITE_C8_D8, 0xC1, 0x78,
    WRITE_C8_D8, 0xC2, 0x78,
    END_WRITE,
    DELAY, 20,
    BEGIN_WRITE,
    WRITE_COMMAND_8, 0xE0,
    WRITE_BYTES, 3, 0x00, 0x1B, 0x02,
    WRITE_COMMAND_8, 0xE1,
    WRITE_BYTES, 11,
    0x08, 0xA0, 0x00, 0x00, 0x07, 0xA0, 0x00, 0x00,
    0x00, 0x44, 0x44,
    WRITE_COMMAND_8, 0xE2,
    WRITE_BYTES, 12,
    0x11, 0x11, 0x44, 0x44, 0xED, 0xA0, 0x00, 0x00,
    0xEC, 0xA0, 0x00, 0x00,
    WRITE_COMMAND_8, 0xE3,
    WRITE_BYTES, 4, 0x00, 0x00, 0x11, 0x11,
    WRITE_C8_D16, 0xE4, 0x44, 0x44,
    WRITE_COMMAND_8, 0xE5,
    WRITE_BYTES, 16,
    0x0A, 0xE9, 0xD8, 0xA0, 0x0C, 0xEB, 0xD8, 0xA0,
    0x0E, 0xED, 0xD8, 0xA0, 0x10, 0xEF, 0xD8, 0xA0,
    WRITE_COMMAND_8, 0xE6,
    WRITE_BYTES, 4, 0x00, 0x00, 0x11, 0x11,
    WRITE_C8_D16, 0xE7, 0x44, 0x44,
    WRITE_COMMAND_8, 0xE8,
    WRITE_BYTES, 16,
    0x09, 0xE8, 0xD8, 0xA0, 0x0B, 0xEA, 0xD8, 0xA0,
    0x0D, 0xEC, 0xD8, 0xA0, 0x0F, 0xEE, 0xD8, 0xA0,
    WRITE_COMMAND_8, 0xEB,
    WRITE_BYTES, 7, 0x02, 0x00, 0xE4, 0xE4, 0x88, 0x00, 0x40,
    WRITE_C8_D16, 0xEC, 0x3C, 0x00,
    WRITE_COMMAND_8, 0xED,
    WRITE_BYTES, 16,
    0xAB, 0x89, 0x76, 0x54, 0x02, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x20, 0x45, 0x67, 0x98, 0xBA,
    WRITE_COMMAND_8, 0xFF,
    WRITE_BYTES, 5, 0x77, 0x01, 0x00, 0x00, 0x00,
    WRITE_C8_D8, 0x36, 0x00,
    WRITE_C8_D8, 0x3A, 0x66,
    WRITE_COMMAND_8, 0x21,
    END_WRITE,
    DELAY, 120,
    BEGIN_WRITE,
    WRITE_COMMAND_8, 0x29,
    END_WRITE};

#if defined(CONFIG_COMPILER_OPTIMIZATION_PERF) && \
    CONFIG_COMPILER_OPTIMIZATION_PERF
constexpr const char* kCompilerOptimization = "O2";
#else
constexpr const char* kCompilerOptimization = "size";
#endif
#if defined(CONFIG_LCD_RGB_RESTART_IN_VSYNC) && \
    CONFIG_LCD_RGB_RESTART_IN_VSYNC
constexpr bool kRestartInVsync = true;
#else
constexpr bool kRestartInVsync = false;
#endif

class WaveshareAtomicRgbDisplay final : public Arduino_RGB_Display {
 public:
  WaveshareAtomicRgbDisplay(uint8_t rotation, Arduino_DataBus* bus)
      : Arduino_RGB_Display(
            480, 480, nullptr, rotation, true, bus, GFX_NOT_DEFINED,
            kPanelInitOperations, sizeof(kPanelInitOperations)) {}

  bool begin(int32_t speed = GFX_NOT_DEFINED) override {
    if (speed != GFX_SKIP_DATABUS_BEGIN && _bus && !_bus->begin()) {
      return false;
    }

    if (_rst != GFX_NOT_DEFINED) {
      pinMode(_rst, OUTPUT);
      digitalWrite(_rst, HIGH);
      delay(100);
      digitalWrite(_rst, LOW);
      delay(120);
      digitalWrite(_rst, HIGH);
      delay(120);
    } else if (_bus) {
      _bus->sendCommand(0x01);
      delay(120);
    }

    if (_bus && _init_operations_len > 0) {
      _bus->batchOperation(
          const_cast<uint8_t*>(_init_operations), _init_operations_len);
    }
    esp_lcd_rgb_panel_config_t config{};
    config.clk_src = LCD_CLK_SRC_DEFAULT;
    config.timings.pclk_hz = kRgbPclkHz;
    config.timings.h_res = 480;
    config.timings.v_res = 480;
    config.timings.hsync_pulse_width = kRgbHsyncPulseWidth;
    config.timings.hsync_back_porch = kRgbHsyncBackPorch;
    config.timings.hsync_front_porch = kRgbHsyncFrontPorch;
    config.timings.vsync_pulse_width = kRgbVsyncPulseWidth;
    config.timings.vsync_back_porch = kRgbVsyncBackPorch;
    config.timings.vsync_front_porch = kRgbVsyncFrontPorch;
    config.timings.flags.hsync_idle_low = 0;
    config.timings.flags.vsync_idle_low = 0;
    config.timings.flags.de_idle_high = 0;
    config.timings.flags.pclk_active_neg = 0;
    config.timings.flags.pclk_idle_high = 0;
    config.data_width = 16;
    config.bits_per_pixel = 16;
    config.num_fbs = 2;
    config.bounce_buffer_size_px = kRgbBounceBufferPixels;
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

  uint16_t* framebuffer(uint8_t index) const {
    return index < 2 ? framebuffers_[index] : nullptr;
  }

 private:
  static bool IRAM_ATTR onFrameComplete(
      esp_lcd_panel_handle_t panel,
      const esp_lcd_rgb_panel_event_data_t* event_data, void* user_ctx) {
    (void)panel;
    (void)event_data;
    auto* self = static_cast<WaveshareAtomicRgbDisplay*>(user_ctx);
    if (self) ++self->frame_complete_count_;
    return false;
  }

  static bool IRAM_ATTR onVsync(
      esp_lcd_panel_handle_t panel,
      const esp_lcd_rgb_panel_event_data_t* event_data, void* user_ctx) {
    (void)panel;
    (void)event_data;
    auto* self = static_cast<WaveshareAtomicRgbDisplay*>(user_ctx);
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

Arduino_SWSPI* g_panel_bus = nullptr;
WaveshareAtomicRgbDisplay* g_gfx = nullptr;

bool g_display_ready = false;
bool g_backlight_ready = false;
bool g_touch_ready = false;
bool g_littlefs_ready = false;
HelperType g_helper_type = HelperType::None;
uint8_t g_brightness = 0;
uint8_t g_applied_brightness = 0;
uint8_t g_rotation = DeviceWaveshareS3TouchLCD4::kProfile.rotation_default;
uint8_t g_touch_address = 0;
uint16_t g_storage_write_depth = 0;
bool g_storage_blackout_active = false;
bool g_storage_restart_required = false;
uint8_t g_storage_restore_brightness = 0;
bool g_touch_active = false;
int16_t g_touch_last_x = 0;
int16_t g_touch_last_y = 0;
uint8_t g_touch_status_error_streak = 0;
uint8_t g_touch_point_error_streak = 0;

void ensureStorageLayout() {
  if (!g_littlefs_ready) return;
  LittleFS.mkdir("/_tile_grids");
  LittleFS.mkdir("/_tile_links");
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

bool writeI2cRegister(uint8_t address, uint8_t reg, uint8_t value, uint8_t retries = 5) {
  for (uint8_t attempt = 0; attempt < retries; ++attempt) {
    Wire.beginTransmission(address);
    Wire.write(reg);
    Wire.write(value);
    if (Wire.endTransmission() == 0) return true;
    delay(10 + attempt * 10);
  }
  return false;
}

bool probeI2cAddress(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

bool initHelperController() {
  if (g_helper_type != HelperType::None) return true;

  recoverI2CBus(kI2cSda, kI2cScl);
  Wire.begin(kI2cSda, kI2cScl, kI2cFrequency);
  delay(20);

  // Probe CH32V003 helper controller first (standard on Rev 4.0)
  if (probeI2cAddress(kCh32Address)) {
    Serial.println("[Device/Waveshare ESP32-S3-Touch-LCD-4] Detected CH32V003 helper MCU (Rev 4.0)");
    g_helper_type = HelperType::CH32V003;

    // Pulse reset & bring up display power rails
    writeI2cRegister(kCh32Address, kCh32RegDirection, kCh32DirDefault);
    writeI2cRegister(kCh32Address, kCh32RegOutput, kCh32OutReset);
    delay(200);
    writeI2cRegister(kCh32Address, kCh32RegDirection, kCh32DirDefault);
    writeI2cRegister(kCh32Address, kCh32RegOutput, kCh32OutDisplayOn);
    delay(200);

    // Turn backlight OFF initially (active-low PWM duty: 0xFF is off, 0x00 is full on)
    writeI2cRegister(kCh32Address, kCh32RegPwm, 0xFF);
    g_backlight_ready = true;
    return true;
  }

  Serial.println("[Device/Waveshare ESP32-S3-Touch-LCD-4] Rev 4.0 CH32V003 helper not found at 0x24");
  return false;
}

void applyBrightness(uint8_t value, bool remember = true) {
  if (remember) g_brightness = value;
  if (!g_backlight_ready && !initHelperController()) return;

  if (g_helper_type == HelperType::CH32V003) {
    // CH32V003 PWM is active-low: 0xFF = off (0% duty), 0x00 = full bright (100% duty)
    const uint8_t pwm_val = (value == 0) ? 0xFF : static_cast<uint8_t>(255u - value);
    writeI2cRegister(kCh32Address, kCh32RegPwm, pwm_val);
  }

  g_applied_brightness = value;
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
  for (size_t i = 0; i < len; ++i) {
    data[i] = static_cast<uint8_t>(Wire.read());
  }
  return true;
}

bool probeTouchAddress(uint8_t address) {
  Wire.beginTransmission(address);
  if (Wire.endTransmission() != 0) return false;
  g_touch_address = address;
  uint8_t product_id[4] = {};
  if (!readTouchRegisters(kTouchProductIdRegister, product_id,
                          sizeof(product_id))) {
    g_touch_address = 0;
    return false;
  }
  Serial.printf("[Device/Waveshare ESP32-S3-Touch-LCD-4] GT911 at 0x%02X, id=%c%c%c%c\n",
                address, product_id[0], product_id[1], product_id[2],
                product_id[3]);
  return true;
}

bool initTouch() {
  if (g_touch_ready) return true;
  Wire.begin(kI2cSda, kI2cScl, kI2cFrequency);

  if (!probeTouchAddress(kTouchAddressPrimary) &&
      !probeTouchAddress(kTouchAddressAlternate)) {
    Serial.println(
        "[Device/Waveshare ESP32-S3-Touch-LCD-4] GT911 not found at 0x5D/0x14");
    return false;
  }

  writeTouchRegister(kTouchStatusRegister, 0);
  g_touch_active = false;
  g_touch_ready = true;
  return true;
}

bool initDisplay() {
  if (g_display_ready) return true;

  g_panel_bus = new Arduino_SWSPI(
      GFX_NOT_DEFINED /* DC */,
      kPanelSpiCs /* CS = 42 */,
      kPanelSpiSck /* SCK = 2 */,
      kPanelSpiMosi /* MOSI = 1 */,
      GFX_NOT_DEFINED /* MISO */);
  g_gfx = new WaveshareAtomicRgbDisplay(g_rotation, g_panel_bus);

  if (!g_panel_bus || !g_gfx || !g_gfx->begin()) {
    Serial.println(
        "[Device/Waveshare ESP32-S3-Touch-LCD-4] ST7701 RGB display init failed");
    return false;
  }

  g_gfx->fillScreen(0x0000);
  g_display_ready = true;
  Serial.printf(
      "[Device/Waveshare ESP32-S3-Touch-LCD-4] Display ready, mode=%s, "
      "panel=%s, PCLK=%u MHz, bounce=%u rows/%u px, XIP=%u, "
      "cache-line=%u B, "
      "opt=%s, VSYNC-restart=%u, PSRAM free=%u KB\n",
      HOMETILES_WAVESHARE_S3_RGB_MODE_LABEL,
      kPanelInitLabel,
      static_cast<unsigned>(kRgbPclkHz / 1000000),
      static_cast<unsigned>(HOMETILES_WAVESHARE_S3_RGB_BOUNCE_ROWS),
      static_cast<unsigned>(kRgbBounceBufferPixels),
      kHasPsramXip ? 1U : 0U,
      kHasCacheLine64 ? 64U : 32U,
      kCompilerOptimization,
      kRestartInVsync ? 1U : 0U,
      static_cast<unsigned>(
          heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
  return true;
}

void mapTouch(uint16_t raw_x, uint16_t raw_y, int16_t& x, int16_t& y) {
  constexpr int16_t kMax = 479;
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

bool DeviceWaveshareS3TouchLCD4::init() {
  Serial.println("[Device/Waveshare ESP32-S3-Touch-LCD-4] Initialising board...");

  if (!psramFound()) {
    Serial.println(
        "[Device/Waveshare ESP32-S3-Touch-LCD-4] ERROR: octal PSRAM not detected");
    return false;
  }
  const uint32_t flash_bytes = ESP.getFlashChipSize();
  const uint32_t psram_bytes = ESP.getPsramSize();
  if (flash_bytes != kExpectedFlashBytes ||
      psram_bytes != kExpectedPsramBytes) {
    Serial.printf(
        "[Device/Waveshare ESP32-S3-Touch-LCD-4] ERROR: expected N16R8, "
        "detected flash=%u MB, PSRAM=%u MB\n",
        static_cast<unsigned>(flash_bytes / (1024U * 1024U)),
        static_cast<unsigned>(psram_bytes / (1024U * 1024U)));
    return false;
  }
  Serial.printf(
      "[Device/Waveshare ESP32-S3-Touch-LCD-4] Flash=%u MB, PSRAM=%u MB\n",
      static_cast<unsigned>(flash_bytes / (1024U * 1024U)),
      static_cast<unsigned>(psram_bytes / (1024U * 1024U)));

  if (!initHelperController()) {
    Serial.println(
        "[Device/Waveshare ESP32-S3-Touch-LCD-4] Helper controller init failed");
    return false;
  }
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

  if (!initTouch()) {
    Serial.println(
        "[Device/Waveshare ESP32-S3-Touch-LCD-4] Touch unavailable; continuing");
  }
  return true;
}

void DeviceWaveshareS3TouchLCD4::update() {
  if (g_gfx) g_gfx->service();
}

void DeviceWaveshareS3TouchLCD4::displayPushPixels(
    int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* data) {
  if (!g_display_ready || !g_gfx || !data || w <= 0 || h <= 0) return;
  g_gfx->draw16bitRGBBitmap(
      static_cast<int16_t>(x), static_cast<int16_t>(y),
      const_cast<uint16_t*>(data), static_cast<int16_t>(w),
      static_cast<int16_t>(h));
}

void DeviceWaveshareS3TouchLCD4::displayPushPixelsDMA(
    int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* data) {
  displayPushPixels(x, y, w, h, data);
}

bool DeviceWaveshareS3TouchLCD4::displayTryFullFramePreview(
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

bool DeviceWaveshareS3TouchLCD4::displayBeginAtomicFrame(
    const char* reason) {
  return g_display_ready && g_gfx && g_gfx->beginAtomicFrame(reason);
}

void DeviceWaveshareS3TouchLCD4::displayWaitDMA() {}

void DeviceWaveshareS3TouchLCD4::displayFillScreen(uint16_t color) {
  if (g_display_ready && g_gfx) g_gfx->fillScreen(color);
}

void DeviceWaveshareS3TouchLCD4::displaySetRotation(uint8_t rotation) {
  g_rotation = rotation & 0x03;
  if (g_display_ready && g_gfx) g_gfx->setRotation(g_rotation);
}

void DeviceWaveshareS3TouchLCD4::setBrightness(uint8_t value) {
  applyBrightness(value);
}

uint8_t DeviceWaveshareS3TouchLCD4::getBrightness() {
  return g_brightness;
}

bool DeviceWaveshareS3TouchLCD4::getTouch(int16_t& x, int16_t& y) {
  if (!g_touch_ready && !initTouch()) return false;

  const auto return_held_point = [&]() {
    if (!g_touch_active) return false;
    x = g_touch_last_x;
    y = g_touch_last_y;
    return true;
  };
  const auto return_held_or_fail_safe = [&](uint8_t& error_streak) {
    if (error_streak < UINT8_MAX) ++error_streak;
    if (!g_touch_active || error_streak < kTouchErrorReleaseThreshold) {
      return return_held_point();
    }
    g_touch_active = false;
    g_touch_status_error_streak = 0;
    g_touch_point_error_streak = 0;
    return false;
  };

  uint8_t status = 0;
  if (!readTouchRegisters(kTouchStatusRegister, &status, 1)) {
    return return_held_or_fail_safe(g_touch_status_error_streak);
  }
  g_touch_status_error_streak = 0;
  if ((status & 0x80) == 0) {
    return return_held_point();
  }

  const uint8_t points = status & 0x0F;
  if (points == 0) {
    writeTouchRegister(kTouchStatusRegister, 0);
    g_touch_active = false;
    g_touch_point_error_streak = 0;
    return false;
  }
  if (points > 5) {
    writeTouchRegister(kTouchStatusRegister, 0);
    return return_held_or_fail_safe(g_touch_point_error_streak);
  }

  uint8_t point[8] = {};
  const bool read_ok =
      readTouchRegisters(kTouchPointRegister, point, sizeof(point));
  writeTouchRegister(kTouchStatusRegister, 0);
  if (!read_ok) {
    return return_held_or_fail_safe(g_touch_point_error_streak);
  }

  const uint16_t raw_x =
      static_cast<uint16_t>(point[1] | (point[2] << 8));
  const uint16_t raw_y =
      static_cast<uint16_t>(point[3] | (point[4] << 8));
  if (raw_x >= 480 || raw_y >= 480) {
    return return_held_or_fail_safe(g_touch_point_error_streak);
  }

  g_touch_point_error_streak = 0;
  mapTouch(raw_x, raw_y, x, y);
  g_touch_last_x = x;
  g_touch_last_y = y;
  if (!g_touch_active) {
    g_touch_active = true;
  }
  return true;
}

void DeviceWaveshareS3TouchLCD4::displaySleep() {
  applyBrightness(0, false);
}

void DeviceWaveshareS3TouchLCD4::displayWake() {
  applyBrightness(g_brightness ? g_brightness : 160, false);
}

void DeviceWaveshareS3TouchLCD4::displayWakeDark() {
  applyBrightness(0, false);
}

void DeviceWaveshareS3TouchLCD4::displayPowerSaveOn() {
  displaySleep();
}

void DeviceWaveshareS3TouchLCD4::displayPowerSaveOff() {
  displayWake();
}

void DeviceWaveshareS3TouchLCD4::displayWaitDisplay() {
  if (g_display_ready && g_gfx) g_gfx->commitAtomicFrame();
}

void DeviceWaveshareS3TouchLCD4::prepareForRestart() {
  applyBrightness(0, false);
  if (g_display_ready && g_gfx) {
    g_gfx->fillScreen(0x0000);
    g_gfx->flush(true);
  }
  delay(20);
}

bool DeviceWaveshareS3TouchLCD4::initSDCard() {
  return false;
}

bool DeviceWaveshareS3TouchLCD4::storageReady() {
  return g_littlefs_ready;
}

fs::FS& DeviceWaveshareS3TouchLCD4::storageFS() {
  return LittleFS;
}

void DeviceWaveshareS3TouchLCD4::storageWriteBegin() {
  if (g_storage_write_depth < UINT16_MAX) {
    ++g_storage_write_depth;
  }
  if (g_storage_write_depth != 1) return;

  if (kHasPsramXip && kHasCacheLine64) return;
  if (!g_display_ready) return;

  g_storage_restart_required = true;
  const bool blackout = g_backlight_ready && g_applied_brightness != 0;
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

void DeviceWaveshareS3TouchLCD4::storageWriteEnd() {
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

bool DeviceWaveshareS3TouchLCD4::sdReady() {
  return false;
}

fs::FS& DeviceWaveshareS3TouchLCD4::sdFS() {
  return LittleFS;
}

bool DeviceWaveshareS3TouchLCD4::suspendSDCardForNetworkTransition() {
  return false;
}

bool DeviceWaveshareS3TouchLCD4::resumeSDCardAfterNetworkTransition() {
  return false;
}

bool DeviceWaveshareS3TouchLCD4::initLittleFS() {
  if (g_littlefs_ready) return true;
  if (!LittleFS.begin(true, "/littlefs", 10, "spiffs")) {
    Serial.println(
        "[Device/Waveshare ESP32-S3-Touch-LCD-4] LittleFS mount failed");
    return false;
  }
  g_littlefs_ready = true;
  ensureStorageLayout();
  Serial.printf(
      "[Device/Waveshare ESP32-S3-Touch-LCD-4] LittleFS ready, total=%u, used=%u\n",
      static_cast<unsigned>(LittleFS.totalBytes()),
      static_cast<unsigned>(LittleFS.usedBytes()));
  return true;
}

void DeviceWaveshareS3TouchLCD4::migrateStorageFromSD() {
  if (!initLittleFS() || LittleFS.exists("/_migrated")) return;

  storageWriteBegin();
  ensureStorageLayout();
  Serial.println("[Storage] SD is not supported by this profile; LittleFS is active");

  File flag = LittleFS.open("/_migrated", FILE_WRITE);
  if (flag) {
    flag.print("1");
    flag.close();
  }
  storageWriteEnd();
}

#endif  // defined(DEVICE_WAVESHARE_S3_TOUCH_LCD_4)
