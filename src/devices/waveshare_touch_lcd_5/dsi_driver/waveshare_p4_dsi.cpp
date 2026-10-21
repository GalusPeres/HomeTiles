#include "src/devices/waveshare_touch_lcd_5/dsi_driver/waveshare_p4_dsi.h"

#include "src/devices/device_select.h"

#if defined(DEVICE_WAVESHARE_P4_DSI_BOARD)

#include <Arduino.h>
#include <LittleFS.h>
#include <cstring>
#include <driver/gpio.h>
#include <driver/ledc.h>
#include <esp_cache.h>
#include <esp_heap_caps.h>
#include <esp_ldo_regulator.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_mipi_dsi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <hal/lcd_types.h>

#include "src/devices/common/p4_dsi_camera_presenter.h"
#include "src/devices/common/p4_dsi_cpu_rotate.h"
#include "src/devices/common/p4_dsi_ui_ppa.h"
#include "src/devices/waveshare_touch_lcd_5/dsi_driver/vendor/gt911.h"
#include "src/devices/waveshare_touch_lcd_5/dsi_driver/vendor/i2c.h"
#include "src/devices/waveshare_touch_lcd_5/dsi_driver/waveshare_p4_dsi_board_select.h"
#include "src/devices/waveshare_touch_lcd_5/dsi_driver/waveshare_p4_dsi_sdmmc.h"

// One line per message, "[Device/<board>] ..." (English, see AGENTS.md).
#define WAVESHARE_P4_DSI_LOG(fmt, ...)                                   \
  Serial.printf("[Device/%s] " fmt "\n", waveshare_p4_dsi::kActiveBoard.log_name, \
                ##__VA_ARGS__)

namespace {

using waveshare_p4_dsi::kActiveBoard;

constexpr const waveshare_p4_dsi::Panel& panel_cfg = kActiveBoard.panel;
constexpr const waveshare_p4_dsi::Backlight& backlight_cfg = kActiveBoard.backlight;

static_assert(panel_cfg.width < panel_cfg.height,
              "The shared Waveshare P4 driver turns a landscape UI into a portrait panel");
static_assert(kActiveBoard.profile.screen_width == panel_cfg.height &&
                  kActiveBoard.profile.screen_height == panel_cfg.width,
              "The landscape profile is the portrait panel turned a quarter");
static_assert(kActiveBoard.profile.rotation_step_mode == Device::RotationStepMode::FlipOnly,
              "The landscape UI is turned 90 or 270 degrees only");
static_assert(backlight_cfg.pwm_bits > 0 && backlight_cfg.pwm_bits <= 14,
              "LEDC backlight resolution");

constexpr ledc_channel_t kBacklightChannel = LEDC_CHANNEL_0;
constexpr ledc_timer_t kBacklightTimer = LEDC_TIMER_1;
constexpr uint32_t kBacklightMaxDuty = (1u << backlight_cfg.pwm_bits) - 1u;
// The profile's floor: raw values up to it are 1 % (device.h).
constexpr uint8_t kBacklightInputMin = kActiveBoard.profile.backlight_input_min;
constexpr uint8_t kBacklightInputMax = 255;
constexpr uint32_t kBootBlackWarmupMs = 90;
constexpr uint32_t kBootBlackGapMs = 60;
constexpr uint32_t kPanelFrameBufferCount = 2;
constexpr size_t kFillChunkRows = 40;
constexpr uintptr_t kCacheLineSize = 64;
constexpr uint32_t kPpaPreviewTimeoutMs = 200;
constexpr uint32_t kPpaPreviewGraceMs = 800;
constexpr uint8_t kTouchReleaseDebounceReads = 0;
constexpr int32_t kTouchJitterThresholdPx = 2;
constexpr uint8_t kTouchSamplePointCount = ESP_LCD_TOUCH_MAX_POINTS;
constexpr uint8_t kInvalidTouchTrackId = 0xFF;

esp_lcd_dsi_bus_handle_t g_dsi_bus = nullptr;
esp_lcd_panel_io_handle_t g_panel_io = nullptr;
esp_lcd_panel_handle_t g_panel = nullptr;
esp_lcd_touch_handle_t g_touch = nullptr;
esp_ldo_channel_handle_t g_mipi_phy_ldo = nullptr;
// The shared wide-band UI PPA policy (Waveshare 8-inch, Guition V2); small
// regions keep the CPU path below.
p4_dsi_ui_ppa::Client g_ui_ppa(kActiveBoard.log_name);
SemaphoreHandle_t g_transfer_done = nullptr;
SemaphoreHandle_t g_refresh_done = nullptr;

DEV_I2C_Port g_i2c = {};
bool g_i2c_ready = false;
uint16_t* g_rotate_buf = nullptr;
size_t g_rotate_buf_pixels = 0;
uint16_t* g_panel_fbs[kPanelFrameBufferCount] = {nullptr};
bool g_panel_fb_ready = false;
p4_dsi_camera_presenter::Presenter g_camera_presenter;
bool g_frame_dirty = false;
int32_t g_dirty_x1 = 0;
int32_t g_dirty_y1 = 0;
int32_t g_dirty_x2 = 0;
int32_t g_dirty_y2 = 0;

uint8_t g_brightness = 200;
bool g_backlight_ready = false;
bool g_sd_init_attempted = false;
bool g_sd_available = false;
uint32_t g_sd_retry_tick_ms = 0;
bool g_littlefs_ready = false;
uint8_t g_rotation = kActiveBoard.profile.rotation_default;
bool g_touch_active = false;
uint8_t g_touch_release_reads = 0;
int32_t g_touch_stable_x = 0;
int32_t g_touch_stable_y = 0;
uint8_t g_touch_active_track_id = kInvalidTouchTrackId;

void log_step(const char* message) {
  WAVESHARE_P4_DSI_LOG("%s", message);
  Serial.flush();
}

bool IRAM_ATTR on_color_trans_done(esp_lcd_panel_handle_t, esp_lcd_dpi_panel_event_data_t*, void* user_ctx) {
  SemaphoreHandle_t sem = static_cast<SemaphoreHandle_t>(user_ctx);
  if (!sem) {
    return false;
  }

  BaseType_t high_task_woken = pdFALSE;
  xSemaphoreGiveFromISR(sem, &high_task_woken);
  return high_task_woken == pdTRUE;
}

bool IRAM_ATTR on_refresh_done(esp_lcd_panel_handle_t, esp_lcd_dpi_panel_event_data_t*, void*) {
  if (!g_refresh_done) {
    return false;
  }

  BaseType_t high_task_woken = pdFALSE;
  xSemaphoreGiveFromISR(g_refresh_done, &high_task_woken);
  return high_task_woken == pdTRUE;
}

void note_ppa_fault() {
  g_ui_ppa.noteFault();
}

void drain_transfer_signal() {
  if (!g_transfer_done) {
    return;
  }
  while (xSemaphoreTake(g_transfer_done, 0) == pdTRUE) {
  }
}

void wait_transfer_done(size_t pixels) {
  if (!g_transfer_done) {
    return;
  }

  const TickType_t timeout = pdMS_TO_TICKS(pixels >= 32768 ? 250 : 50);
  if (xSemaphoreTake(g_transfer_done, timeout) != pdTRUE) {
    WAVESHARE_P4_DSI_LOG("draw transfer timeout (%u px)", static_cast<unsigned>(pixels));
  }
}

void flush_cache_for_dma(const void* ptr, size_t size) {
  if (!ptr || size == 0) {
    return;
  }
  const uintptr_t start = reinterpret_cast<uintptr_t>(ptr);
  const uintptr_t aligned_start = start & ~(kCacheLineSize - 1);
  const uintptr_t end = start + size;
  const uintptr_t aligned_end = (end + kCacheLineSize - 1) & ~(kCacheLineSize - 1);
  if (aligned_end <= aligned_start) {
    return;
  }
  esp_cache_msync(reinterpret_cast<void*>(aligned_start),
                  aligned_end - aligned_start,
                  ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_TYPE_DATA);
}

bool ensure_rotate_buffer(size_t pixels) {
  if (pixels == 0) {
    return false;
  }
  if (g_rotate_buf && g_rotate_buf_pixels >= pixels) {
    return true;
  }

  if (g_rotate_buf) {
    heap_caps_free(g_rotate_buf);
    g_rotate_buf = nullptr;
    g_rotate_buf_pixels = 0;
  }

  const size_t bytes = pixels * sizeof(uint16_t);
  g_rotate_buf = static_cast<uint16_t*>(
      heap_caps_aligned_alloc(64, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA));
  if (!g_rotate_buf) {
    g_rotate_buf = static_cast<uint16_t*>(
        heap_caps_aligned_alloc(64, bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
  }
  if (!g_rotate_buf) {
    WAVESHARE_P4_DSI_LOG("Rotate buffer allocation failed (%u bytes)", static_cast<unsigned>(bytes));
    return false;
  }

  g_rotate_buf_pixels = pixels;
  return true;
}

bool draw_physical(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* data) {
  if (!g_panel || !data || w <= 0 || h <= 0) {
    return false;
  }

  const int32_t panel_w = panel_cfg.width;
  const int32_t panel_h = panel_cfg.height;
  if (x < 0 || y < 0 || (x + w) > panel_w || (y + h) > panel_h) {
    WAVESHARE_P4_DSI_LOG("Reject out-of-range draw x=%ld y=%ld w=%ld h=%ld",
                         static_cast<long>(x), static_cast<long>(y),
                         static_cast<long>(w), static_cast<long>(h));
    return false;
  }

  drain_transfer_signal();
  flush_cache_for_dma(data, static_cast<size_t>(w) * static_cast<size_t>(h) * sizeof(uint16_t));
  const esp_err_t err = esp_lcd_panel_draw_bitmap(g_panel, x, y, x + w, y + h, data);
  if (err != ESP_OK) {
    WAVESHARE_P4_DSI_LOG("draw_bitmap failed: %d", static_cast<int>(err));
    return false;
  }
  wait_transfer_done(static_cast<size_t>(w) * static_cast<size_t>(h));
  return true;
}

size_t panel_pixel_count() {
  return static_cast<size_t>(panel_cfg.width) * static_cast<size_t>(panel_cfg.height);
}

size_t panel_frame_bytes() {
  return panel_pixel_count() * sizeof(uint16_t);
}

uint16_t* panel_fb() {
  return g_panel_fb_ready ? g_camera_presenter.activeFramebuffer() : nullptr;
}

void reset_dirty_rect() {
  g_frame_dirty = false;
  g_dirty_x1 = 0;
  g_dirty_y1 = 0;
  g_dirty_x2 = 0;
  g_dirty_y2 = 0;
}

void clear_panel_framebuffer(uint16_t color) {
  uint16_t* fb = panel_fb();
  if (!fb) {
    return;
  }

  const size_t pixels = panel_pixel_count();
  if (color == 0) {
    std::memset(fb, 0, panel_frame_bytes());
  } else {
    for (size_t i = 0; i < pixels; ++i) {
      fb[i] = color;
    }
  }
  flush_cache_for_dma(fb, panel_frame_bytes());
  reset_dirty_rect();
}

void mark_dirty_rect(int32_t x, int32_t y, int32_t w, int32_t h) {
  if (!g_frame_dirty) {
    g_dirty_x1 = x;
    g_dirty_y1 = y;
    g_dirty_x2 = x + w - 1;
    g_dirty_y2 = y + h - 1;
    g_frame_dirty = true;
    return;
  }

  if (x < g_dirty_x1) g_dirty_x1 = x;
  if (y < g_dirty_y1) g_dirty_y1 = y;
  const int32_t x2 = x + w - 1;
  const int32_t y2 = y + h - 1;
  if (x2 > g_dirty_x2) g_dirty_x2 = x2;
  if (y2 > g_dirty_y2) g_dirty_y2 = y2;
}

void flush_framebuffer_rect(const uint16_t* fb, int32_t x, int32_t y, int32_t w, int32_t h) {
  if (!fb || w <= 0 || h <= 0) {
    return;
  }

  const size_t stride = panel_cfg.width;
  const size_t row_bytes = static_cast<size_t>(w) * sizeof(uint16_t);
  for (int32_t row = 0; row < h; ++row) {
    flush_cache_for_dma(fb + (static_cast<size_t>(y + row) * stride) + static_cast<size_t>(x),
                        row_bytes);
  }
}

void copy_rect_to_fb(uint16_t* dst, int32_t x, int32_t y, int32_t w, int32_t h,
                     const uint16_t* src) {
  if (!dst || !src || w <= 0 || h <= 0) {
    return;
  }

  const size_t stride = panel_cfg.width;
  const size_t row_bytes = static_cast<size_t>(w) * sizeof(uint16_t);
  for (int32_t row = 0; row < h; ++row) {
    std::memcpy(dst + (static_cast<size_t>(y + row) * stride) + static_cast<size_t>(x),
                src + static_cast<size_t>(row) * static_cast<size_t>(w),
                row_bytes);
  }
  flush_framebuffer_rect(dst, x, y, w, h);
}

bool init_panel_framebuffer() {
  if (!g_panel) {
    return false;
  }

  void* fb0 = nullptr;
  void* fb1 = nullptr;
  const esp_err_t err = esp_lcd_dpi_panel_get_frame_buffer(g_panel,
                                                           kPanelFrameBufferCount,
                                                           &fb0, &fb1);
  if (err != ESP_OK || !fb0 || !fb1) {
    WAVESHARE_P4_DSI_LOG("Panel framebuffers unavailable err=%d fb0=%p fb1=%p",
                         static_cast<int>(err), fb0, fb1);
    g_panel_fb_ready = false;
    return false;
  }

  g_panel_fbs[0] = static_cast<uint16_t*>(fb0);
  g_panel_fbs[1] = static_cast<uint16_t*>(fb1);
  const p4_dsi_camera_presenter::Config presenter_config{
      panel_cfg.width,
      panel_cfg.height,
      p4_dsi_camera_presenter::Transform::Portrait90Or270,
      kPpaPreviewTimeoutMs,
      kPpaPreviewGraceMs,
      kActiveBoard.log_name,
  };
  g_panel_fb_ready = g_camera_presenter.init(
      presenter_config, g_panel, g_refresh_done, g_panel_fbs[0],
      g_panel_fbs[1]);
  if (!g_panel_fb_ready) {
    WAVESHARE_P4_DSI_LOG("Shared camera presenter init failed");
    return false;
  }
  reset_dirty_rect();
  WAVESHARE_P4_DSI_LOG("Panel framebuffers OK fb0=%p fb1=%p", fb0, fb1);
  return true;
}

bool write_physical_to_panel(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* data) {
  if (!g_panel_fb_ready || !data || w <= 0 || h <= 0) {
    return false;
  }

  const int32_t panel_w = panel_cfg.width;
  const int32_t panel_h = panel_cfg.height;
  if (x < 0 || y < 0 || (x + w) > panel_w || (y + h) > panel_h) {
    WAVESHARE_P4_DSI_LOG("Reject out-of-range framebuffer draw x=%ld y=%ld w=%ld h=%ld",
                         static_cast<long>(x), static_cast<long>(y),
                         static_cast<long>(w), static_cast<long>(h));
    return false;
  }

  uint16_t* fb = panel_fb();
  if (!fb) {
    return false;
  }

  copy_rect_to_fb(fb, x, y, w, h, data);
  mark_dirty_rect(x, y, w, h);
  g_camera_presenter.noteUiWrite(x, y, w, h, false);
  return true;
}

// The landscape UI area (x, y) lands at panel (w - 1 - y, x); rotation 2 at
// the mirrored place. The touch mapping in getTouch() is its exact inverse.
bool draw_landscape_area(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* data) {
  if (!data || w <= 0 || h <= 0) {
    return false;
  }

  const int32_t logical_w = panel_cfg.height;
  const int32_t logical_h = panel_cfg.width;
  if (x < 0 || y < 0 || (x + w) > logical_w || (y + h) > logical_h) {
    WAVESHARE_P4_DSI_LOG("Reject out-of-range landscape draw x=%ld y=%ld w=%ld h=%ld",
                         static_cast<long>(x), static_cast<long>(y),
                         static_cast<long>(w), static_cast<long>(h));
    return false;
  }

  int32_t dst_x = 0;
  int32_t dst_y = 0;
  const int32_t dst_w = h;
  const int32_t dst_h = w;
  if (g_rotation & 0x02) {
    dst_x = y;
    dst_y = logical_w - x - w;
  } else {
    dst_x = logical_h - y - h;
    dst_y = x;
  }

  // Wide bands go through the shared UI PPA policy (p4_dsi_ui_ppa).
  const auto ppa_result = g_ui_ppa.rotate(
      g_camera_presenter, panel_cfg.width, panel_cfg.height,
      x, y, w, h, data, g_rotation);
  if (ppa_result == p4_dsi_ui_ppa::Result::Drawn) {
    mark_dirty_rect(dst_x, dst_y, dst_w, dst_h);
    return true;
  }
  if (ppa_result == p4_dsi_ui_ppa::Result::Failed) return false;

  // Small regions: rotate straight into the panel framebuffer
  // (p4_dsi_cpu_rotate.h) instead of through the PSRAM rotate buffer.
  if (g_panel_fb_ready) {
    if (uint16_t* fb = panel_fb()) {
      p4_dsi_cpu_rotate::rotate_into(fb, panel_cfg.width, dst_x, dst_y, w, h,
                                     data, (g_rotation & 0x02) != 0);
      flush_framebuffer_rect(fb, dst_x, dst_y, dst_w, dst_h);
      mark_dirty_rect(dst_x, dst_y, dst_w, dst_h);
      g_camera_presenter.noteUiWrite(dst_x, dst_y, dst_w, dst_h, false);
      return true;
    }
  }

  const size_t pixel_count = static_cast<size_t>(w) * static_cast<size_t>(h);
  if (!ensure_rotate_buffer(pixel_count)) {
    return false;
  }

  if (g_rotation & 0x02) {
    for (int32_t sy = 0; sy < h; ++sy) {
      const uint16_t* src_row = data + static_cast<size_t>(sy) * w;
      for (int32_t sx = 0; sx < w; ++sx) {
        g_rotate_buf[static_cast<size_t>(w - 1 - sx) * h + sy] = src_row[sx];
      }
    }
  } else {
    for (int32_t sy = 0; sy < h; ++sy) {
      const uint16_t* src_row = data + static_cast<size_t>(sy) * w;
      for (int32_t sx = 0; sx < w; ++sx) {
        g_rotate_buf[static_cast<size_t>(sx) * h + (h - 1 - sy)] = src_row[sx];
      }
    }
  }

  if (write_physical_to_panel(dst_x, dst_y, dst_w, dst_h, g_rotate_buf)) {
    return true;
  }

  return draw_physical(dst_x, dst_y, dst_w, dst_h, g_rotate_buf);
}

// Holds the panel in reset before a restart: the reset line's asserted
// level comes from the board (the HX8394 of the LCD-5 resets on high).
void hold_panel_in_reset() {
  if (panel_cfg.reset_gpio == GPIO_NUM_NC) {
    return;
  }

  gpio_set_direction(panel_cfg.reset_gpio, GPIO_MODE_OUTPUT);
  gpio_set_level(panel_cfg.reset_gpio, panel_cfg.reset_active_high ? 1 : 0);
}

void apply_backlight(uint8_t value) {
  if (!g_backlight_ready) {
    return;
  }

  uint32_t brightness_percent = 0;
  if (value == 0) {
    brightness_percent = 0;
  } else if (value <= kBacklightInputMin) {
    brightness_percent = 1;
  } else if (value >= kBacklightInputMax) {
    brightness_percent = 100;
  } else {
    const uint32_t span = static_cast<uint32_t>(kBacklightInputMax - kBacklightInputMin);
    brightness_percent =
        1u + ((static_cast<uint32_t>(value - kBacklightInputMin) * 99u) +
              (span / 2u)) /
                 span;
  }

  // Like Waveshare's BSPs: a duty from an integer percentage, at most one
  // count below the full period, so the PWM never becomes a static level.
  uint32_t duty = (brightness_percent * kBacklightMaxDuty) / 100u;
  if (backlight_cfg.active_low) {
    duty = kBacklightMaxDuty - duty;
  }

  ledc_set_duty(LEDC_LOW_SPEED_MODE, kBacklightChannel, duty);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, kBacklightChannel);
}

bool init_backlight() {
  if (g_backlight_ready) {
    return true;
  }

  gpio_set_direction(backlight_cfg.gpio, GPIO_MODE_OUTPUT);
  gpio_set_level(backlight_cfg.gpio, backlight_cfg.active_low ? 1 : 0);

  ledc_timer_config_t timer_cfg = {};
  timer_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
  timer_cfg.timer_num = kBacklightTimer;
  timer_cfg.duty_resolution = static_cast<ledc_timer_bit_t>(backlight_cfg.pwm_bits);
  timer_cfg.freq_hz = backlight_cfg.pwm_hz;
  timer_cfg.clk_cfg = LEDC_AUTO_CLK;
  if (ledc_timer_config(&timer_cfg) != ESP_OK) {
    WAVESHARE_P4_DSI_LOG("Backlight timer init failed");
    return false;
  }

  ledc_channel_config_t ch_cfg = {};
  ch_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
  ch_cfg.channel = kBacklightChannel;
  ch_cfg.timer_sel = kBacklightTimer;
  ch_cfg.gpio_num = backlight_cfg.gpio;
  ch_cfg.duty = backlight_cfg.active_low ? kBacklightMaxDuty : 0;
  ch_cfg.hpoint = 0;
  if (ledc_channel_config(&ch_cfg) != ESP_OK) {
    WAVESHARE_P4_DSI_LOG("Backlight channel init failed");
    return false;
  }

  g_backlight_ready = true;
  apply_backlight(0);
  return true;
}

bool init_i2c() {
  if (g_i2c_ready) {
    return true;
  }

  g_i2c = DEV_I2C_Init(kActiveBoard.i2c_sda, kActiveBoard.i2c_scl);
  if (!g_i2c.bus) {
    WAVESHARE_P4_DSI_LOG("I2C init failed");
    return false;
  }
  g_i2c_ready = true;
  return true;
}

bool init_mipi_phy_power() {
  if (g_mipi_phy_ldo) {
    return true;
  }

  esp_ldo_channel_config_t ldo_cfg = {};
  ldo_cfg.chan_id = panel_cfg.mipi_phy_ldo_channel;
  ldo_cfg.voltage_mv = panel_cfg.mipi_phy_ldo_mv;

  log_step("MIPI PHY LDO init start");
  const esp_err_t err = esp_ldo_acquire_channel(&ldo_cfg, &g_mipi_phy_ldo);
  if (err != ESP_OK) {
    WAVESHARE_P4_DSI_LOG("MIPI PHY LDO init failed err=%d", static_cast<int>(err));
    return false;
  }
  log_step("MIPI PHY LDO init OK");
  return true;
}

bool init_display() {
  if (g_panel) {
    return true;
  }
  if (!init_mipi_phy_power()) {
    return false;
  }

  log_step("Allocating transfer semaphore");
  g_transfer_done = xSemaphoreCreateBinary();
  g_refresh_done = xSemaphoreCreateBinary();
  if (!g_transfer_done || !g_refresh_done) {
    WAVESHARE_P4_DSI_LOG("Transfer semaphore allocation failed");
    return false;
  }
  log_step("Transfer semaphore OK");

  esp_lcd_dsi_bus_config_t bus_cfg = {};
  bus_cfg.bus_id = 0;
  bus_cfg.num_data_lanes = panel_cfg.lane_count;
#if CONFIG_ESP_REV_MIN_FULL >= 300
  // ESP32-P4 v3.x builds ("v3.00 or newer") accept XTAL/APLL/CPLL/SPLL/MPLL
  // only; PLL_F20M aborts in the low-level MIPI DSI clock-source switch.
  bus_cfg.phy_clk_src = MIPI_DSI_PHY_PLLREF_CLK_SRC_DEFAULT;
#else
  // Pre-v3 builds ("Before v3.00") accept PLL_F20M/RC_FAST/PLL_F25M only;
  // XTAL/default aborts in the low-level MIPI DSI clock-source switch.
  bus_cfg.phy_clk_src = MIPI_DSI_PHY_PLLREF_CLK_SRC_PLL_F20M;
#endif
  bus_cfg.lane_bit_rate_mbps = static_cast<float>(panel_cfg.lane_bit_rate_mbps);
  WAVESHARE_P4_DSI_LOG("DSI bus init start lanes=%u lane=%u Mbps",
                       static_cast<unsigned>(panel_cfg.lane_count),
                       static_cast<unsigned>(panel_cfg.lane_bit_rate_mbps));
  Serial.flush();
  esp_err_t err = esp_lcd_new_dsi_bus(&bus_cfg, &g_dsi_bus);
  if (err != ESP_OK) {
    WAVESHARE_P4_DSI_LOG("DSI bus init failed err=%d", static_cast<int>(err));
    return false;
  }
  log_step("DSI bus init OK");

  esp_lcd_dbi_io_config_t dbi_cfg = {};
  dbi_cfg.virtual_channel = 0;
  dbi_cfg.lcd_cmd_bits = 8;
  dbi_cfg.lcd_param_bits = 8;
  log_step("DSI DBI IO init start");
  err = esp_lcd_new_panel_io_dbi(g_dsi_bus, &dbi_cfg, &g_panel_io);
  if (err != ESP_OK) {
    WAVESHARE_P4_DSI_LOG("DSI DBI IO init failed err=%d", static_cast<int>(err));
    return false;
  }
  log_step("DSI DBI IO init OK");

  esp_lcd_dpi_panel_config_t dpi_cfg = {};
  dpi_cfg.virtual_channel = 0;
  dpi_cfg.dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
  dpi_cfg.dpi_clock_freq_mhz = static_cast<float>(panel_cfg.dpi_clock_hz) / 1000000.0f;
  dpi_cfg.pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565;
  dpi_cfg.in_color_format = LCD_COLOR_FMT_RGB565;
  dpi_cfg.out_color_format = LCD_COLOR_FMT_RGB565;
  dpi_cfg.num_fbs = kPanelFrameBufferCount;
  dpi_cfg.video_timing.h_size = panel_cfg.width;
  dpi_cfg.video_timing.v_size = panel_cfg.height;
  dpi_cfg.video_timing.hsync_pulse_width = panel_cfg.hsync_pulse_width;
  dpi_cfg.video_timing.hsync_back_porch = panel_cfg.hsync_back_porch;
  dpi_cfg.video_timing.hsync_front_porch = panel_cfg.hsync_front_porch;
  dpi_cfg.video_timing.vsync_pulse_width = panel_cfg.vsync_pulse_width;
  dpi_cfg.video_timing.vsync_back_porch = panel_cfg.vsync_back_porch;
  dpi_cfg.video_timing.vsync_front_porch = panel_cfg.vsync_front_porch;
  // LVGL writes the active framebuffer. The second driver-owned buffer is
  // reserved for atomic camera and full-frame preview swaps.
  dpi_cfg.flags.use_dma2d = true;

  esp_lcd_panel_dev_config_t panel_dev_cfg = {};
  panel_dev_cfg.reset_gpio_num = panel_cfg.reset_gpio;
  panel_dev_cfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
  panel_dev_cfg.data_endian = LCD_RGB_DATA_ENDIAN_LITTLE;
  panel_dev_cfg.bits_per_pixel = 16;
  panel_dev_cfg.flags.reset_active_high = panel_cfg.reset_active_high ? 1 : 0;

  WAVESHARE_P4_DSI_LOG("%s panel create start", panel_cfg.controller);
  err = panel_cfg.create(g_panel_io, panel_dev_cfg, g_dsi_bus, &dpi_cfg,
                         panel_cfg.lane_count, &g_panel);
  if (err != ESP_OK || !g_panel) {
    WAVESHARE_P4_DSI_LOG("%s panel create failed err=%d", panel_cfg.controller,
                         static_cast<int>(err));
    g_panel = nullptr;
    return false;
  }
  log_step("Panel create OK");

  if (init_panel_framebuffer()) {
    clear_panel_framebuffer(0x0000);
  }

  esp_lcd_dpi_panel_event_callbacks_t cbs = {};
  cbs.on_color_trans_done = on_color_trans_done;
  cbs.on_refresh_done = on_refresh_done;
  log_step("DPI callback register start");
  err = esp_lcd_dpi_panel_register_event_callbacks(g_panel, &cbs, g_transfer_done);
  if (err != ESP_OK) {
    WAVESHARE_P4_DSI_LOG("DPI callback register failed err=%d", static_cast<int>(err));
    return false;
  }
  log_step("DPI callback register OK");

  log_step("Panel reset start");
  err = esp_lcd_panel_reset(g_panel);
  if (err != ESP_OK) {
    WAVESHARE_P4_DSI_LOG("Panel reset failed err=%d", static_cast<int>(err));
    return false;
  }
  log_step("Panel reset OK");

  log_step("Panel init start");
  err = esp_lcd_panel_init(g_panel);
  if (err != ESP_OK) {
    WAVESHARE_P4_DSI_LOG("Panel init failed err=%d", static_cast<int>(err));
    return false;
  }
  log_step("Panel init OK");

  log_step("Panel display on start");
  err = esp_lcd_panel_disp_on_off(g_panel, true);
  if (err != ESP_OK) {
    WAVESHARE_P4_DSI_LOG("Panel display on failed err=%d", static_cast<int>(err));
    return false;
  }
  log_step("Panel display on OK");

  g_ui_ppa.init();

  return true;
}

bool init_touch() {
  if (g_touch) {
    return true;
  }
  if (!init_i2c()) {
    return false;
  }

  for (uint8_t attempt = 1; attempt <= 3; ++attempt) {
    g_touch = touch_gt911_init(g_i2c, panel_cfg.width, panel_cfg.height);
    if (g_touch) {
      return true;
    }
    WAVESHARE_P4_DSI_LOG("Touch init attempt %u failed", static_cast<unsigned>(attempt));
    delay(150);
  }

  WAVESHARE_P4_DSI_LOG("Touch init failed");
  return false;
}

void ensure_storage_layout() {
  if (!g_littlefs_ready) {
    return;
  }
  LittleFS.mkdir("/_tile_grids");
  LittleFS.mkdir("/_tile_links");
}

bool copy_sd_file_to_littlefs(const char* path) {
  File source = WaveshareP4DsiSDMMC.open(path, FILE_READ);
  if (!source) {
    return false;
  }
  File destination = LittleFS.open(path, FILE_WRITE);
  if (!destination) {
    source.close();
    return false;
  }

  uint8_t buffer[512];
  while (source.available()) {
    const size_t count = source.read(buffer, sizeof(buffer));
    if (count == 0) {
      break;
    }
    destination.write(buffer, count);
  }
  destination.close();
  source.close();
  return true;
}

void copy_sd_directory_to_littlefs(const char* directory_path) {
  File directory = WaveshareP4DsiSDMMC.open(directory_path);
  if (!directory || !directory.isDirectory()) {
    return;
  }

  LittleFS.mkdir(directory_path);
  File entry = directory.openNextFile();
  while (entry) {
    const String full_path = String(directory_path) + "/" + entry.name();
    if (entry.isDirectory()) {
      copy_sd_directory_to_littlefs(full_path.c_str());
    } else if (copy_sd_file_to_littlefs(full_path.c_str())) {
      Serial.printf("[Storage] Migrated: %s (%u bytes)\n", full_path.c_str(),
                    static_cast<unsigned>(entry.size()));
    }
    entry = directory.openNextFile();
  }
}

}  // namespace

bool waveshare_p4_dsi::init() {
  WAVESHARE_P4_DSI_LOG("Initialising board...");

  if (!init_backlight()) {
    return false;
  }
  apply_backlight(0);

  if (!init_display()) {
    return false;
  }
  WAVESHARE_P4_DSI_LOG("Display OK");

  if (!init_touch()) {
    WAVESHARE_P4_DSI_LOG("Touch init failed, continuing without touch");
  } else {
    WAVESHARE_P4_DSI_LOG("Touch OK");
  }

  // Keep the boot path dark until HomeTiles has rendered the first complete
  // splash frame. Showing this single live DPI framebuffer too early exposes
  // intermediate LVGL/flush bands as a short stair-step flash.
  displayFillScreen(0x0000);
  displayWakeDark();
  delay(kBootBlackWarmupMs);
  displaySleep();
  delay(kBootBlackGapMs);
  displayFillScreen(0x0000);
  apply_backlight(0);
  WAVESHARE_P4_DSI_LOG("Init complete");
  return true;
}

void waveshare_p4_dsi::update() {
}

void waveshare_p4_dsi::pausePpaFor(uint32_t duration_ms) {
  g_ui_ppa.pauseFor(duration_ms);
}

bool waveshare_p4_dsi::ppaCooldownActive() {
  return g_ui_ppa.cooldownActive();
}

void waveshare_p4_dsi::displayPushPixels(int32_t x, int32_t y, int32_t w, int32_t h,
                                         const uint16_t* data) {
  // The upright layout: straight into the panel (the shared way).
  if (g_camera_presenter.upright()) {
    g_camera_presenter.drawUpright(x, y, w, h, data, g_rotation);
    return;
  }
  draw_landscape_area(x, y, w, h, data);
}

void waveshare_p4_dsi::displayPushPixelsDMA(int32_t x, int32_t y, int32_t w, int32_t h,
                                            const uint16_t* data) {
  displayPushPixels(x, y, w, h, data);
}

bool waveshare_p4_dsi::displayTryFullFramePreview(
    int32_t x, int32_t y, int32_t w, int32_t h,
    int32_t source_stride, const uint16_t* data, size_t data_size,
    bool byte_swap) {
  const auto runtime = g_ui_ppa.runtime(note_ppa_fault);
  const bool presented = g_camera_presenter.present(
      x, y, w, h, source_stride, data, data_size, byte_swap, g_rotation,
      runtime);
  if (presented) g_ui_ppa.noteSuccess();
  return presented;
}

void waveshare_p4_dsi::displayEndFullFramePreview() {
  g_camera_presenter.end();
}

bool waveshare_p4_dsi::displayFullFrameInfo(uint16_t& width, uint16_t& height,
                                            uint16_t& turn_cw) {
  return g_panel_fb_ready &&
         g_camera_presenter.fullFrameInfo(g_rotation, width, height, turn_cw);
}

bool waveshare_p4_dsi::displayBeginFullFrames() {
  return g_panel_fb_ready && g_camera_presenter.beginFullFrames();
}

uint16_t* waveshare_p4_dsi::displayAcquireFullFrame(size_t& bytes) {
  bytes = 0;
  return g_panel_fb_ready ? g_camera_presenter.acquireFullFrame(bytes) : nullptr;
}

bool waveshare_p4_dsi::displaySubmitFullFrame() {
  return g_panel_fb_ready && g_camera_presenter.submitFullFrame();
}

void waveshare_p4_dsi::displayEndFullFrames() {
  if (g_panel_fb_ready) g_camera_presenter.endFullFrames();
}

void waveshare_p4_dsi::displayWaitDMA() {
}

void waveshare_p4_dsi::displayCommit() {
  if (!g_panel || !g_panel_fb_ready || !g_frame_dirty) {
    return;
  }

  // Dirty pixels were already written into the active DPI framebuffer.
  reset_dirty_rect();
}

void waveshare_p4_dsi::displayFillScreen(uint16_t color) {
  if (!g_panel) {
    return;
  }

  if (g_panel_fb_ready) {
    uint16_t* buf = panel_fb();
    if (buf) {
      const size_t pixels = panel_pixel_count();
      for (size_t i = 0; i < pixels; ++i) {
        buf[i] = color;
      }
      flush_cache_for_dma(buf, panel_frame_bytes());
      g_camera_presenter.noteUiWrite(
          0, 0, panel_cfg.width, panel_cfg.height, false);
    }

    reset_dirty_rect();
    return;
  }

  const size_t chunk_pixels = static_cast<size_t>(panel_cfg.width) * kFillChunkRows;
  if (!ensure_rotate_buffer(chunk_pixels)) {
    return;
  }

  for (size_t i = 0; i < chunk_pixels; ++i) {
    g_rotate_buf[i] = color;
  }

  for (int32_t y = 0; y < panel_cfg.height; y += static_cast<int32_t>(kFillChunkRows)) {
    int32_t rows = static_cast<int32_t>(kFillChunkRows);
    if (y + rows > panel_cfg.height) {
      rows = panel_cfg.height - y;
    }
    draw_physical(0, y, panel_cfg.width, rows, g_rotate_buf);
  }
}

void waveshare_p4_dsi::displaySetRotation(uint8_t rotation) {
  g_rotation = rotation & 0x03;
}

void waveshare_p4_dsi::displaySetUpright(bool upright) {
  // The UI and the camera popup's frames without the quarter turn.
  g_camera_presenter.setUpright(upright);
}

void waveshare_p4_dsi::setBrightness(uint8_t value) {
  g_brightness = value;
  apply_backlight(value);
}

uint8_t waveshare_p4_dsi::getBrightness() {
  return g_brightness;
}

bool waveshare_p4_dsi::getTouch(int16_t& x, int16_t& y) {
  if (!g_touch) {
    return false;
  }

  uint16_t px[kTouchSamplePointCount] = {0};
  uint16_t py[kTouchSamplePointCount] = {0};
  uint16_t strength[kTouchSamplePointCount] = {0};
  uint8_t track_ids[kTouchSamplePointCount] = {0};
  uint8_t count = 0;

  esp_lcd_touch_read_data(g_touch);
  const bool touched = esp_lcd_touch_get_coordinates(g_touch, px, py, strength, &count, kTouchSamplePointCount);
  if (!touched || count == 0) {
    if (g_touch_active && g_touch_release_reads < kTouchReleaseDebounceReads) {
      ++g_touch_release_reads;
      x = static_cast<int16_t>(g_touch_stable_x);
      y = static_cast<int16_t>(g_touch_stable_y);
      return true;
    }
    g_touch_active = false;
    g_touch_release_reads = 0;
    g_touch_active_track_id = kInvalidTouchTrackId;
    return false;
  }

  g_touch_release_reads = 0;

  if (esp_lcd_touch_get_track_id(g_touch, track_ids, count) != ESP_OK) {
    for (uint8_t i = 0; i < count; ++i) {
      track_ids[i] = kInvalidTouchTrackId;
    }
  }

  // The GT911 reports native portrait panel coordinates; the landscape point
  // is the exact inverse of draw_landscape_area().
  const auto to_landscape = [](uint16_t touch_x, uint16_t touch_y, int32_t& out_x,
                               int32_t& out_y) {
    if (g_rotation & 0x02) {
      out_x = static_cast<int32_t>(panel_cfg.height) - 1 - static_cast<int32_t>(touch_y);
      out_y = static_cast<int32_t>(touch_x);
    } else {
      out_x = static_cast<int32_t>(touch_y);
      out_y = static_cast<int32_t>(panel_cfg.width) - 1 - static_cast<int32_t>(touch_x);
    }
  };

  uint8_t selected = 0;
  bool selected_found = false;
  if (g_touch_active && g_touch_active_track_id != kInvalidTouchTrackId) {
    for (uint8_t i = 0; i < count; ++i) {
      if (track_ids[i] == g_touch_active_track_id) {
        selected = i;
        selected_found = true;
        break;
      }
    }
  }
  if (!selected_found && g_touch_active && count > 1) {
    uint32_t best_distance = 0xFFFFFFFFu;
    for (uint8_t i = 0; i < count; ++i) {
      int32_t candidate_x = 0;
      int32_t candidate_y = 0;
      to_landscape(px[i], py[i], candidate_x, candidate_y);
      const int32_t dx = candidate_x - g_touch_stable_x;
      const int32_t dy = candidate_y - g_touch_stable_y;
      const uint32_t distance = static_cast<uint32_t>((dx >= 0 ? dx : -dx) + (dy >= 0 ? dy : -dy));
      if (distance < best_distance) {
        best_distance = distance;
        selected = i;
        selected_found = true;
      }
    }
  }
  if (!selected_found && count > 1) {
    uint16_t best_strength = 0;
    for (uint8_t i = 0; i < count; ++i) {
      if (strength[i] >= best_strength) {
        best_strength = strength[i];
        selected = i;
        selected_found = true;
      }
    }
  }

  g_touch_active_track_id = track_ids[selected];

  int32_t mapped_x = 0;
  int32_t mapped_y = 0;
  to_landscape(px[selected], py[selected], mapped_x, mapped_y);

  const int32_t logical_w = static_cast<int32_t>(panel_cfg.height);
  const int32_t logical_h = static_cast<int32_t>(panel_cfg.width);

  if (mapped_x < 0) mapped_x = 0;
  if (mapped_y < 0) mapped_y = 0;
  if (mapped_x >= logical_w) mapped_x = logical_w - 1;
  if (mapped_y >= logical_h) mapped_y = logical_h - 1;

  if (!g_touch_active) {
    g_touch_stable_x = mapped_x;
    g_touch_stable_y = mapped_y;
    g_touch_active = true;
  } else {
    const int32_t dx = mapped_x - g_touch_stable_x;
    const int32_t dy = mapped_y - g_touch_stable_y;
    const int32_t adx = dx >= 0 ? dx : -dx;
    const int32_t ady = dy >= 0 ? dy : -dy;

    // Ignore tiny GT911 wobble so taps feel more deterministic; the
    // controller filters on its own, so larger moves pass at once.
    if (adx > kTouchJitterThresholdPx || ady > kTouchJitterThresholdPx) {
      g_touch_stable_x = mapped_x;
      g_touch_stable_y = mapped_y;
    }
  }

  x = static_cast<int16_t>(g_touch_stable_x);
  y = static_cast<int16_t>(g_touch_stable_y);
  return true;
}

void waveshare_p4_dsi::displaySleep() {
  if (g_panel) {
    esp_lcd_panel_disp_on_off(g_panel, false);
  }
  apply_backlight(0);
}

void waveshare_p4_dsi::displayWake() {
  if (g_panel) {
    esp_lcd_panel_disp_on_off(g_panel, true);
  }
  apply_backlight(g_brightness);
}

void waveshare_p4_dsi::displayWakeDark() {
  if (g_panel) {
    esp_lcd_panel_disp_on_off(g_panel, true);
  }
  apply_backlight(0);
}

void waveshare_p4_dsi::displayPowerSaveOn() {
  displaySleep();
}

void waveshare_p4_dsi::displayPowerSaveOff() {
  displayWake();
}

void waveshare_p4_dsi::displayWaitDisplay() {
}

void waveshare_p4_dsi::prepareForRestart() {
  if (g_panel) {
    displayFillScreen(0x0000);
    esp_lcd_panel_disp_on_off(g_panel, false);
  }

  hold_panel_in_reset();
  apply_backlight(0);
  ledc_stop(LEDC_LOW_SPEED_MODE, kBacklightChannel, backlight_cfg.active_low ? 1u : 0u);
  gpio_set_direction(backlight_cfg.gpio, GPIO_MODE_OUTPUT);
  gpio_set_level(backlight_cfg.gpio, backlight_cfg.active_low ? 1 : 0);
}

i2c_master_bus_handle_t waveshare_p4_dsi::sharedI2cBus() {
  return g_i2c_ready ? g_i2c.bus : nullptr;
}

bool waveshare_p4_dsi::initSDCard() {
  if (g_sd_available && WaveshareP4DsiSDMMC.cardType() != CARD_NONE) {
    return true;
  }

  const uint32_t now = millis();
  if (g_sd_init_attempted && (now - g_sd_retry_tick_ms) < 1500) {
    return false;
  }
  g_sd_init_attempted = true;
  g_sd_retry_tick_ms = now;

  WaveshareP4DsiSDMMC.end();

  if (!WaveshareP4DsiSDMMC.begin(kActiveBoard.sd_ldo_channel, "/sdcard", false,
                                 kActiveBoard.sd_max_freq_khz, 5)) {
    g_sd_available = false;
    WAVESHARE_P4_DSI_LOG("SD card mount failed");
    return false;
  }

  const uint8_t card_type = WaveshareP4DsiSDMMC.cardType();
  if (card_type == CARD_NONE) {
    g_sd_available = false;
    WAVESHARE_P4_DSI_LOG("SD card absent after mount");
    WaveshareP4DsiSDMMC.end();
    return false;
  }

  g_sd_available = true;
  WAVESHARE_P4_DSI_LOG("SD card OK, type=%u, size=%llu MB",
                       static_cast<unsigned>(card_type),
                       static_cast<unsigned long long>(WaveshareP4DsiSDMMC.cardSize() / (1024ULL * 1024ULL)));
  return true;
}

bool waveshare_p4_dsi::storageReady() {
  return g_littlefs_ready;
}

fs::FS& waveshare_p4_dsi::storageFS() {
  return LittleFS;
}

bool waveshare_p4_dsi::sdReady() {
  return initSDCard();
}

fs::FS& waveshare_p4_dsi::sdFS() {
  return WaveshareP4DsiSDMMC;
}

bool waveshare_p4_dsi::suspendSDCardForNetworkTransition() {
  const bool was_mounted =
      g_sd_available && WaveshareP4DsiSDMMC.cardType() != CARD_NONE;
  if (!was_mounted) return false;

  WaveshareP4DsiSDMMC.end();
  g_sd_available = false;
  g_sd_init_attempted = false;
  WAVESHARE_P4_DSI_LOG("SD card suspended for network transition");
  return true;
}

bool waveshare_p4_dsi::resumeSDCardAfterNetworkTransition() {
  g_sd_available = false;
  g_sd_init_attempted = false;
  const bool mounted = initSDCard();
  WAVESHARE_P4_DSI_LOG("SD card remount after network transition: %s",
                       mounted ? "OK" : "failed");
  return mounted;
}

bool waveshare_p4_dsi::initLittleFS() {
  if (g_littlefs_ready) {
    return true;
  }
  if (!LittleFS.begin(true, "/littlefs", 10, "spiffs")) {
    WAVESHARE_P4_DSI_LOG("LittleFS mount failed");
    return false;
  }
  g_littlefs_ready = true;
  WAVESHARE_P4_DSI_LOG("LittleFS ready, total=%u, used=%u",
                       static_cast<unsigned>(LittleFS.totalBytes()),
                       static_cast<unsigned>(LittleFS.usedBytes()));
  ensure_storage_layout();
  return true;
}

void waveshare_p4_dsi::migrateStorageFromSD() {
  if (!initLittleFS()) {
    return;
  }
  if (LittleFS.exists("/_migrated")) {
    return;
  }

  LittleFS.mkdir("/_tile_grids");
  LittleFS.mkdir("/_tile_links");

  if (initSDCard()) {
    Serial.println("[Storage] Migrating data from SD to LittleFS...");
    copy_sd_directory_to_littlefs("/_tile_grids");
    copy_sd_directory_to_littlefs("/_tile_links");
    Serial.println("[Storage] Migration complete");
  } else {
    Serial.println("[Storage] No SD card, starting fresh");
  }

  File flag = LittleFS.open("/_migrated", FILE_WRITE);
  if (flag) {
    flag.print("1");
    flag.close();
  }
}

#endif  // defined(DEVICE_WAVESHARE_P4_DSI_BOARD)
