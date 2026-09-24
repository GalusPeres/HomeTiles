#include "src/devices/guition_jc4880p443_portrait/device_guition_jc4880p443_portrait.h"

#include "src/devices/device_select.h"

#if defined(DEVICE_GUITION_JC4880P443_PORTRAIT)

#include <Arduino.h>
#include <LittleFS.h>
#include <cstring>
#include <utility>
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
#include <driver/ppa.h>
#include <hal/lcd_types.h>

#include "src/core/display/dma2d_arbiter.h"
#include "src/devices/common/p4_dsi_camera_presenter.h"
#include "src/devices/guition_jc4880p443_portrait/sdmmc.h"
#include "src/devices/guition_jc4880p443_portrait/vendor/displays_config.h"
#include "src/devices/guition_jc4880p443_portrait/vendor/gt911.h"
#include "src/devices/guition_jc4880p443_portrait/vendor/i2c.h"
#include "src/devices/guition_jc4880p443_portrait/vendor/st7701/esp_lcd_st7701.h"
#include "src/devices/guition_jc4880p443_portrait/vendor/panel_init_cmds.h"

namespace {

constexpr gpio_num_t kBacklightPin = GPIO_NUM_23;
constexpr ledc_channel_t kBacklightChannel = LEDC_CHANNEL_0;
constexpr ledc_timer_t kBacklightTimer = LEDC_TIMER_1;
constexpr uint32_t kBacklightFreq = 5000;
constexpr ledc_timer_bit_t kBacklightBits = LEDC_TIMER_10_BIT;
constexpr bool kBacklightActiveLow = false;
constexpr uint8_t kBacklightInputMin = 1;
constexpr uint8_t kBacklightInputMax = 255;
constexpr uint32_t kBootBlackWarmupMs = 90;
constexpr uint32_t kBootBlackGapMs = 60;
constexpr uint32_t kPanelLaneCount = 2;
constexpr uint32_t kPanelFrameBufferCount = 2;
constexpr size_t kFillChunkRows = 40;
constexpr uintptr_t kCacheLineSize = 64;
constexpr uint8_t kTouchReleaseDebounceReads = 0;
constexpr int32_t kTouchJitterThresholdPx = 2;
constexpr uint8_t kTouchSamplePointCount = ESP_LCD_TOUCH_MAX_POINTS;
constexpr uint8_t kInvalidTouchTrackId = 0xFF;
// Upper bound for one PPA rotate. A real rotate is microseconds (a band) up to a
// few ms (full screen); if it ever blocks longer than this the engine is stuck,
// so we bail out and rotate that band on the CPU instead of freezing forever.
constexpr uint32_t kPpaRotateTimeoutMs = 200;
constexpr uint32_t kPpaPreviewGraceMs = 800;
constexpr uint32_t kPpaFaultCooldownMs = 1200;
// A single stray fault just cools the engine down; this many consecutive faults
// proves the pending slot is wedged and triggers a full client reset (self-heal).
constexpr uint8_t kPpaFaultsBeforeReset = 2;
// If re-registering the PPA client fails (e.g. no internal heap for the driver
// state at that moment), don't stay on the slow CPU rotate until reboot: retry
// the re-init on this interval from the rotate path.
constexpr uint32_t kPpaReinitRetryMs = 1000;

esp_lcd_dsi_bus_handle_t g_dsi_bus = nullptr;
esp_lcd_panel_io_handle_t g_panel_io = nullptr;
esp_lcd_panel_handle_t g_panel = nullptr;
esp_lcd_touch_handle_t g_touch = nullptr;
esp_ldo_channel_handle_t g_mipi_phy_ldo = nullptr;
ppa_client_handle_t g_ppa_handle = nullptr;
SemaphoreHandle_t g_transfer_done = nullptr;
SemaphoreHandle_t g_refresh_done = nullptr;
SemaphoreHandle_t g_ppa_done = nullptr;   // PPA rotate completion (non-blocking mode)
bool g_ppa_async_ready = false;           // true once the PPA done-callback is armed
uint32_t g_ppa_cooldown_until_ms = 0;     // while active, flushes use CPU rotate
uint8_t g_ppa_consecutive_faults = 0;     // resets to 0 on any successful rotate
uint32_t g_ppa_reinit_at_ms = 0;          // 0 = no pending re-init retry
bool g_ppa_reset_pending = false;          // true while a wedged client is being retired

DEV_I2C_Port g_i2c = {};
bool g_i2c_ready = false;
bool g_pmic_ready = false;
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
uint8_t g_rotation = DeviceGuitionJC4880P443Portrait::kProfile.rotation_default;
bool g_touch_active = false;
uint8_t g_touch_release_reads = 0;
int32_t g_touch_stable_x = 0;
int32_t g_touch_stable_y = 0;
uint8_t g_touch_active_track_id = kInvalidTouchTrackId;

bool ppa_cooldown_active() {
  return g_ppa_cooldown_until_ms != 0 &&
         static_cast<int32_t>(millis() - g_ppa_cooldown_until_ms) < 0;
}

void pause_ppa_for(uint32_t duration_ms) {
  if (duration_ms == 0) {
    return;
  }
  const uint32_t until = millis() + duration_ms;
  if (g_ppa_cooldown_until_ms == 0 ||
      static_cast<int32_t>(until - g_ppa_cooldown_until_ms) > 0) {
    g_ppa_cooldown_until_ms = until;
  }
}

void log_step(const char* message) {
  Serial.print("[Device/GuitionJC4880P443Portrait] ");
  Serial.println(message);
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

// Fired from the PPA ISR when a non-blocking rotate finishes. user_ctx is the
// semaphore we passed via oper.user_data so the waiting camera presenter
// can wake up.
bool IRAM_ATTR on_ppa_trans_done(ppa_client_handle_t, ppa_event_data_t*, void* user_ctx) {
  SemaphoreHandle_t sem = static_cast<SemaphoreHandle_t>(user_ctx);
  if (!sem) {
    return false;
  }

  BaseType_t high_task_woken = pdFALSE;
  xSemaphoreGiveFromISR(sem, &high_task_woken);
  return high_task_woken == pdTRUE;
}

// Tear down a wedged PPA client and bring up a fresh one so the camera
// presenter heals itself instead of staying off until a power cycle. A stalled
// non-blocking rotate keeps its transaction in the client's single pending slot;
// ppa_unregister_client() is non-blocking and refuses while that slot is busy.
// Keep the old handle in that case and retry retiring it after the pending transfer
// has drained. Dropping that handle leaks a client slot and can leave the camera
// preview disabled until reboot.
void reset_ppa_client() {
  g_ppa_async_ready = false;
  g_ppa_reset_pending = true;
  ppa_client_handle_t old = g_ppa_handle;
  if (old) {
    const esp_err_t unregister_err = ppa_unregister_client(old);
    if (unregister_err != ESP_OK) {
      g_ppa_handle = old;
      g_ppa_reinit_at_ms = millis() + kPpaReinitRetryMs;
      if (!g_ppa_reinit_at_ms) g_ppa_reinit_at_ms = 1;
      Serial.printf("[Device/GuitionJC4880P443Portrait] PPA client busy err=%d, retry reset in %lu ms\n",
                    static_cast<int>(unregister_err),
                    static_cast<unsigned long>(kPpaReinitRetryMs));
      return;
    }
  }
  g_ppa_handle = nullptr;

  ppa_client_config_t ppa_cfg = {};
  ppa_cfg.oper_type = PPA_OPERATION_SRM;
  ppa_client_handle_t fresh = nullptr;
  const esp_err_t reg_err = ppa_register_client(&ppa_cfg, &fresh);
  if (reg_err != ESP_OK || !fresh) {
    g_ppa_reinit_at_ms = millis() + kPpaReinitRetryMs;
    if (!g_ppa_reinit_at_ms) g_ppa_reinit_at_ms = 1;
    Serial.printf("[Device/GuitionJC4880P443Portrait] PPA reset failed err=%d (int free=%u KB, largest=%u KB), retry in %lu ms\n",
                  static_cast<int>(reg_err),
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                  static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024),
                  static_cast<unsigned long>(kPpaReinitRetryMs));
    return;
  }
  if (!g_ppa_done) {
    // First registration may have failed before the semaphore existed; without
    // it we would fall back to blocking PPA, which can freeze on a stall.
    g_ppa_done = xSemaphoreCreateBinary();
  }
  if (g_ppa_done) {
    ppa_event_callbacks_t ppa_cbs = {};
    ppa_cbs.on_trans_done = on_ppa_trans_done;
    g_ppa_async_ready =
        (ppa_client_register_event_callbacks(fresh, &ppa_cbs) == ESP_OK);
  }
  g_ppa_handle = fresh;
  if (!g_ppa_async_ready) {
    g_ppa_reinit_at_ms = millis() + kPpaReinitRetryMs;
    if (!g_ppa_reinit_at_ms) g_ppa_reinit_at_ms = 1;
    Serial.println("[Device/GuitionJC4880P443Portrait] PPA timeout callback unavailable, retrying client reset");
    return;
  }
  g_ppa_reinit_at_ms = 0;
  g_ppa_reset_pending = false;
  Serial.println("[Device/GuitionJC4880P443Portrait] PPA client reset after fault");
}

// Called when a PPA rotate stalls or is rejected. The first stray fault only cools
// the engine down (a stuck transaction may still drain on its own); the next fault
// in a row proves the slot is wedged and resets the client. The caller always falls
// through to the CPU rotate, so the frame still lands.
void note_ppa_fault() {
  if (++g_ppa_consecutive_faults >= kPpaFaultsBeforeReset) {
    reset_ppa_client();
    g_ppa_consecutive_faults = 0;
  }
  pause_ppa_for(kPpaFaultCooldownMs);
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
    Serial.printf("[Device/GuitionJC4880P443Portrait] draw transfer timeout (%u px)\n",
                  static_cast<unsigned>(pixels));
  }
}

void drain_refresh_signal() {
  if (!g_refresh_done) {
    return;
  }
  while (xSemaphoreTake(g_refresh_done, 0) == pdTRUE) {
  }
}

void wait_refresh_done() {
  if (!g_refresh_done) {
    return;
  }

  if (xSemaphoreTake(g_refresh_done, pdMS_TO_TICKS(50)) != pdTRUE) {
    Serial.println("[Device/GuitionJC4880P443Portrait] refresh timeout");
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
    Serial.printf("[Device/GuitionJC4880P443Portrait] Rotate buffer allocation failed (%u bytes)\n",
                  static_cast<unsigned>(bytes));
    return false;
  }

  g_rotate_buf_pixels = pixels;
  return true;
}

bool draw_physical(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* data) {
  if (!g_panel || !data || w <= 0 || h <= 0) {
    return false;
  }

  const int32_t panel_w = display_cfg.width;
  const int32_t panel_h = display_cfg.height;
  if (x < 0 || y < 0 || (x + w) > panel_w || (y + h) > panel_h) {
    Serial.printf("[Device/GuitionJC4880P443Portrait] Reject out-of-range draw x=%ld y=%ld w=%ld h=%ld\n",
                  static_cast<long>(x), static_cast<long>(y),
                  static_cast<long>(w), static_cast<long>(h));
    return false;
  }

  drain_transfer_signal();
  flush_cache_for_dma(data, static_cast<size_t>(w) * static_cast<size_t>(h) * sizeof(uint16_t));
  const esp_err_t err = esp_lcd_panel_draw_bitmap(g_panel, x, y, x + w, y + h, data);
  if (err != ESP_OK) {
    Serial.printf("[Device/GuitionJC4880P443Portrait] draw_bitmap failed: %d\n", static_cast<int>(err));
    return false;
  }
  wait_transfer_done(static_cast<size_t>(w) * static_cast<size_t>(h));
  return true;
}

size_t panel_pixel_count() {
  return static_cast<size_t>(display_cfg.width) * static_cast<size_t>(display_cfg.height);
}

size_t panel_frame_bytes() {
  return panel_pixel_count() * sizeof(uint16_t);
}

uint16_t* panel_fb() {
  return g_panel_fb_ready ? g_camera_presenter.activeFramebuffer() : nullptr;
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
  g_frame_dirty = false;
  g_dirty_x1 = 0;
  g_dirty_y1 = 0;
  g_dirty_x2 = 0;
  g_dirty_y2 = 0;
}

void reset_dirty_rect() {
  g_frame_dirty = false;
  g_dirty_x1 = 0;
  g_dirty_y1 = 0;
  g_dirty_x2 = 0;
  g_dirty_y2 = 0;
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

  const size_t stride = display_cfg.width;
  const size_t row_bytes = static_cast<size_t>(w) * sizeof(uint16_t);
  for (int32_t row = 0; row < h; ++row) {
    flush_cache_for_dma(fb + (static_cast<size_t>(y + row) * stride) + static_cast<size_t>(x),
                        row_bytes);
  }
}

void invalidate_framebuffer_rect(uint16_t* fb, int32_t x, int32_t y, int32_t w, int32_t h) {
  if (!fb || w <= 0 || h <= 0) {
    return;
  }

  const size_t stride = display_cfg.width;
  const size_t row_bytes = static_cast<size_t>(w) * sizeof(uint16_t);
  for (int32_t row = 0; row < h; ++row) {
    uint16_t* row_ptr = fb + (static_cast<size_t>(y + row) * stride) + static_cast<size_t>(x);
    const uintptr_t start = reinterpret_cast<uintptr_t>(row_ptr);
    const uintptr_t aligned_start = start & ~(kCacheLineSize - 1);
    const uintptr_t end = start + row_bytes;
    const uintptr_t aligned_end = (end + kCacheLineSize - 1) & ~(kCacheLineSize - 1);
    if (aligned_end <= aligned_start) {
      continue;
    }
    esp_cache_msync(reinterpret_cast<void*>(aligned_start),
                    aligned_end - aligned_start,
                    ESP_CACHE_MSYNC_FLAG_DIR_M2C |
                        ESP_CACHE_MSYNC_FLAG_INVALIDATE |
                        ESP_CACHE_MSYNC_FLAG_TYPE_DATA);
  }
}

void copy_rect_to_fb(uint16_t* dst, int32_t x, int32_t y, int32_t w, int32_t h,
                     const uint16_t* src) {
  if (!dst || !src || w <= 0 || h <= 0) {
    return;
  }

  const size_t stride = display_cfg.width;
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
    Serial.printf("[Device/GuitionJC4880P443Portrait] Panel framebuffers unavailable err=%d fb0=%p fb1=%p\n",
                  static_cast<int>(err), fb0, fb1);
    g_panel_fb_ready = false;
    return false;
  }

  g_panel_fbs[0] = static_cast<uint16_t*>(fb0);
  g_panel_fbs[1] = static_cast<uint16_t*>(fb1);
  const p4_dsi_camera_presenter::Config presenter_config{
      display_cfg.width,
      display_cfg.height,
      p4_dsi_camera_presenter::Transform::Native0Or180,
      kPpaRotateTimeoutMs,
      kPpaPreviewGraceMs,
      "GuitionJC4880P443Portrait",
  };
  g_panel_fb_ready = g_camera_presenter.init(
      presenter_config, g_panel, g_refresh_done, g_panel_fbs[0],
      g_panel_fbs[1]);
  if (!g_panel_fb_ready) {
    Serial.println(
        "[Device/GuitionJC4880P443Portrait] Shared camera presenter init failed");
    return false;
  }
  reset_dirty_rect();
  Serial.printf("[Device/GuitionJC4880P443Portrait] Panel framebuffers OK fb0=%p fb1=%p\n",
                fb0, fb1);
  return true;
}

bool write_physical_to_panel(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* data) {
  if (!g_panel_fb_ready || !data || w <= 0 || h <= 0) {
    return false;
  }

  const int32_t panel_w = display_cfg.width;
  const int32_t panel_h = display_cfg.height;
  if (x < 0 || y < 0 || (x + w) > panel_w || (y + h) > panel_h) {
    Serial.printf("[Device/GuitionJC4880P443Portrait] Reject out-of-range framebuffer draw x=%ld y=%ld w=%ld h=%ld\n",
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

void hold_panel_reset_low() {
  if (display_cfg.lcd_rst < 0) {
    return;
  }

  const gpio_num_t rst_pin = static_cast<gpio_num_t>(display_cfg.lcd_rst);
  gpio_set_direction(rst_pin, GPIO_MODE_OUTPUT);
  gpio_set_level(rst_pin, 0);
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

  constexpr uint32_t kMaxDuty = (1u << 10) - 1u;
  uint32_t duty = (brightness_percent * kMaxDuty) / 100u;
  if (kBacklightActiveLow) {
    duty = kMaxDuty - duty;
  }

  ledc_set_duty(LEDC_LOW_SPEED_MODE, kBacklightChannel, duty);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, kBacklightChannel);
}

bool init_backlight() {
  if (g_backlight_ready) {
    return true;
  }

  gpio_set_direction(kBacklightPin, GPIO_MODE_OUTPUT);
  gpio_set_level(kBacklightPin, kBacklightActiveLow ? 1 : 0);

  ledc_timer_config_t timer_cfg = {};
  timer_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
  timer_cfg.timer_num = kBacklightTimer;
  timer_cfg.duty_resolution = kBacklightBits;
  timer_cfg.freq_hz = kBacklightFreq;
  timer_cfg.clk_cfg = LEDC_AUTO_CLK;
  if (ledc_timer_config(&timer_cfg) != ESP_OK) {
    Serial.println("[Device/GuitionJC4880P443Portrait] Backlight timer init failed");
    return false;
  }

  ledc_channel_config_t ch_cfg = {};
  ch_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
  ch_cfg.channel = kBacklightChannel;
  ch_cfg.timer_sel = kBacklightTimer;
  ch_cfg.gpio_num = kBacklightPin;
  ch_cfg.duty = kBacklightActiveLow ? ((1u << 10) - 1u) : 0;
  ch_cfg.hpoint = 0;
  if (ledc_channel_config(&ch_cfg) != ESP_OK) {
    Serial.println("[Device/GuitionJC4880P443Portrait] Backlight channel init failed");
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

  g_i2c = DEV_I2C_Init();
  if (!g_i2c.bus) {
    Serial.println("[Device/GuitionJC4880P443Portrait] I2C init failed");
    return false;
  }
  g_i2c_ready = true;
  return true;
}

bool init_pmic() {
  if (g_pmic_ready) {
    return true;
  }

  // The Guition JC4880P443 exposes a dedicated active-high backlight GPIO
  // (GPIO23, MP3202 boost driver) and shares I2C0 with the GT911 touch and
  // ES8311 audio devices. There is no I2C backlight PMIC to probe, so keep
  // PMIC init as a no-op here.
  g_pmic_ready = true;
  return true;
}

bool init_mipi_phy_power() {
  if (g_mipi_phy_ldo) {
    return true;
  }

  esp_ldo_channel_config_t ldo_cfg = {};
  ldo_cfg.chan_id = 3;
  ldo_cfg.voltage_mv = 2500;

  log_step("MIPI PHY LDO init start");
  const esp_err_t err = esp_ldo_acquire_channel(&ldo_cfg, &g_mipi_phy_ldo);
  if (err != ESP_OK) {
    Serial.printf("[Device/GuitionJC4880P443Portrait] MIPI PHY LDO init failed err=%d\n",
                  static_cast<int>(err));
    return false;
  }
  log_step("MIPI PHY LDO init OK");
  return true;
}

bool init_display() {
  if (g_panel) {
    return true;
  }
  if (!init_pmic()) {
    return false;
  }
  if (!init_mipi_phy_power()) {
    return false;
  }

  log_step("Allocating transfer semaphore");
  g_transfer_done = xSemaphoreCreateBinary();
  g_refresh_done = xSemaphoreCreateBinary();
  if (!g_transfer_done || !g_refresh_done) {
    Serial.println("[Device/GuitionJC4880P443Portrait] Transfer semaphore allocation failed");
    return false;
  }
  log_step("Transfer semaphore OK");

  esp_lcd_dsi_bus_config_t bus_cfg = {};
  bus_cfg.bus_id = 0;
  bus_cfg.num_data_lanes = kPanelLaneCount;
  // The Arduino board must be selected as "Before v3.00" for this hardware.
  // That ESP32-P4 path accepts PLL_F20M/RC_FAST/PLL_F25M only; XTAL/default
  // aborts in the low-level MIPI DSI clock-source switch.
  bus_cfg.phy_clk_src = MIPI_DSI_PHY_PLLREF_CLK_SRC_PLL_F20M;
  bus_cfg.lane_bit_rate_mbps = static_cast<float>(display_cfg.lane_bit_rate);
  Serial.printf("[Device/GuitionJC4880P443Portrait] DSI bus init start lane=%u Mbps\n",
                static_cast<unsigned>(display_cfg.lane_bit_rate));
  Serial.flush();
  esp_err_t err = esp_lcd_new_dsi_bus(&bus_cfg, &g_dsi_bus);
  if (err != ESP_OK) {
    Serial.printf("[Device/GuitionJC4880P443Portrait] DSI bus init failed err=%d\n", static_cast<int>(err));
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
    Serial.printf("[Device/GuitionJC4880P443Portrait] DSI DBI IO init failed err=%d\n", static_cast<int>(err));
    return false;
  }
  log_step("DSI DBI IO init OK");

  esp_lcd_dpi_panel_config_t dpi_cfg = {};
  dpi_cfg.virtual_channel = 0;
  dpi_cfg.dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
  dpi_cfg.dpi_clock_freq_mhz = static_cast<float>(display_cfg.prefer_speed) / 1000000.0f;
  dpi_cfg.pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565;
  dpi_cfg.in_color_format = LCD_COLOR_FMT_RGB565;
  dpi_cfg.out_color_format = LCD_COLOR_FMT_RGB565;
  dpi_cfg.num_fbs = kPanelFrameBufferCount;
  dpi_cfg.video_timing.h_size = display_cfg.width;
  dpi_cfg.video_timing.v_size = display_cfg.height;
  dpi_cfg.video_timing.hsync_pulse_width = display_cfg.hsync_pulse_width;
  dpi_cfg.video_timing.hsync_back_porch = display_cfg.hsync_back_porch;
  dpi_cfg.video_timing.hsync_front_porch = display_cfg.hsync_front_porch;
  dpi_cfg.video_timing.vsync_pulse_width = display_cfg.vsync_pulse_width;
  dpi_cfg.video_timing.vsync_back_porch = display_cfg.vsync_back_porch;
  dpi_cfg.video_timing.vsync_front_porch = display_cfg.vsync_front_porch;
  // LVGL writes the active framebuffer. The second driver-owned buffer is
  // reserved for atomic camera and full-frame preview swaps.
  dpi_cfg.flags.use_dma2d = true;

  st7701_vendor_config_t vendor_cfg = {};
  vendor_cfg.init_cmds = kGuitionSt7701Init4880P443;
  vendor_cfg.init_cmds_size =
      static_cast<uint16_t>(kGuitionSt7701Init4880P443Count);
  vendor_cfg.mipi_config.dsi_bus = g_dsi_bus;
  vendor_cfg.mipi_config.dpi_config = &dpi_cfg;
  vendor_cfg.flags.use_mipi_interface = 1;

  esp_lcd_panel_dev_config_t panel_cfg = {};
  panel_cfg.reset_gpio_num = display_cfg.lcd_rst;
  panel_cfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
  panel_cfg.data_endian = LCD_RGB_DATA_ENDIAN_LITTLE;
  panel_cfg.bits_per_pixel = 16;
  panel_cfg.flags.reset_active_high = false;
  panel_cfg.vendor_config = &vendor_cfg;

  log_step("ST7701 panel create start");
  err = esp_lcd_new_panel_st7701(g_panel_io, &panel_cfg, &g_panel);
  if (err != ESP_OK) {
    Serial.printf("[Device/GuitionJC4880P443Portrait] Panel create failed err=%d\n",
                  static_cast<int>(err));
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
    Serial.printf("[Device/GuitionJC4880P443Portrait] DPI callback register failed err=%d\n", static_cast<int>(err));
    return false;
  }
  log_step("DPI callback register OK");

  log_step("Panel reset start");
  err = esp_lcd_panel_reset(g_panel);
  if (err != ESP_OK) {
    Serial.printf("[Device/GuitionJC4880P443Portrait] Panel reset failed err=%d\n", static_cast<int>(err));
    return false;
  }
  log_step("Panel reset OK");

  log_step("Panel init start");
  err = esp_lcd_panel_init(g_panel);
  if (err != ESP_OK) {
    Serial.printf("[Device/GuitionJC4880P443Portrait] Panel init failed err=%d\n", static_cast<int>(err));
    return false;
  }
  log_step("Panel init OK");

  log_step("Panel display on start");
  err = esp_lcd_panel_disp_on_off(g_panel, true);
  if (err != ESP_OK) {
    Serial.printf("[Device/GuitionJC4880P443Portrait] Panel display on failed err=%d\n", static_cast<int>(err));
    return false;
  }
  log_step("Panel display on OK");

  ppa_client_config_t ppa_cfg = {};
  ppa_cfg.oper_type = PPA_OPERATION_SRM;
  const esp_err_t ppa_err = ppa_register_client(&ppa_cfg, &g_ppa_handle);
  if (ppa_err != ESP_OK) {
    Serial.printf("[Device/GuitionJC4880P443Portrait] PPA client register failed err=%d, camera preview disabled until PPA re-init succeeds\n",
                  static_cast<int>(ppa_err));
    g_ppa_handle = nullptr;
    g_ppa_reset_pending = true;
    g_ppa_reinit_at_ms = millis() + kPpaReinitRetryMs;
    if (!g_ppa_reinit_at_ms) g_ppa_reinit_at_ms = 1;
  } else {
    g_ppa_reset_pending = false;
    log_step("PPA client registered");
    // Arm the done-callback so the camera presenter can use the
    // timeout-safe non-blocking path. If this fails, keep the preview
    // disabled and retry the client instead of entering an unbounded
    // blocking PPA call.
    g_ppa_done = xSemaphoreCreateBinary();
    if (g_ppa_done) {
      ppa_event_callbacks_t ppa_cbs = {};
      ppa_cbs.on_trans_done = on_ppa_trans_done;
      if (ppa_client_register_event_callbacks(g_ppa_handle, &ppa_cbs) == ESP_OK) {
        g_ppa_async_ready = true;
        log_step("PPA timeout-safe mode ready");
      } else {
        Serial.println("[Device/GuitionJC4880P443Portrait] PPA event cb register failed, retrying safely");
      }
    }
    if (!g_ppa_async_ready) {
      g_ppa_reset_pending = true;
      g_ppa_reinit_at_ms = millis() + kPpaReinitRetryMs;
      if (!g_ppa_reinit_at_ms) g_ppa_reinit_at_ms = 1;
    }
  }

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
    g_touch = touch_gt911_init(g_i2c);
    if (g_touch) {
      return true;
    }
    Serial.printf("[Device/GuitionJC4880P443Portrait] Touch init attempt %u failed\n",
                  static_cast<unsigned>(attempt));
    delay(150);
  }

  if (!g_touch) {
    Serial.println("[Device/GuitionJC4880P443Portrait] Touch init failed");
    return false;
  }

  return true;
}

void ensure_storage_layout() {
  if (!g_littlefs_ready) {
    return;
  }
  LittleFS.mkdir("/_tile_grids");
  LittleFS.mkdir("/_tile_links");
  LittleFS.mkdir("/icons");
}

bool draw_portrait_area(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* data) {
  if (!data || w <= 0 || h <= 0) {
    return false;
  }

  const int32_t panel_w = display_cfg.width;
  const int32_t panel_h = display_cfg.height;
  if (x < 0 || y < 0 || (x + w) > panel_w || (y + h) > panel_h) {
    Serial.printf("[Device/GuitionJC4880P443Portrait] Reject out-of-range portrait draw x=%ld y=%ld w=%ld h=%ld\n",
                  static_cast<long>(x), static_cast<long>(y),
                  static_cast<long>(w), static_cast<long>(h));
    return false;
  }

  if (g_ppa_reinit_at_ms &&
      static_cast<int32_t>(millis() - g_ppa_reinit_at_ms) >= 0) {
    g_ppa_reinit_at_ms = 0;
    reset_ppa_client();
  }

  if (!(g_rotation & 0x02)) {
    if (write_physical_to_panel(x, y, w, h, data)) {
      return true;
    }
    return draw_physical(x, y, w, h, data);
  }

  const int32_t dst_x = panel_w - x - w;
  const int32_t dst_y = panel_h - y - h;
  const size_t pixel_count = static_cast<size_t>(w) * static_cast<size_t>(h);
  if (!ensure_rotate_buffer(pixel_count)) {
    return false;
  }
  for (int32_t sy = 0; sy < h; ++sy) {
    const uint16_t* src_row = data + static_cast<size_t>(sy) * static_cast<size_t>(w);
    uint16_t* dst_row =
        g_rotate_buf + static_cast<size_t>(h - 1 - sy) * static_cast<size_t>(w);
    for (int32_t sx = 0; sx < w; ++sx) {
      dst_row[w - 1 - sx] = src_row[sx];
    }
  }

  if (write_physical_to_panel(dst_x, dst_y, w, h, g_rotate_buf)) {
    return true;
  }
  return draw_physical(dst_x, dst_y, w, h, g_rotate_buf);
}

}  // namespace

bool DeviceGuitionJC4880P443Portrait::init() {
  Serial.println("[Device/GuitionJC4880P443Portrait] Initialising board...");

  if (!init_backlight()) {
    return false;
  }
  apply_backlight(0);

  if (!init_display()) {
    return false;
  }
  Serial.println("[Device/GuitionJC4880P443Portrait] Display OK");

  if (!init_touch()) {
    Serial.println("[Device/GuitionJC4880P443Portrait] Touch init failed, continuing without touch");
  } else {
    Serial.println("[Device/GuitionJC4880P443Portrait] Touch OK");
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
  Serial.println("[Device/GuitionJC4880P443Portrait] Init complete");
  return true;
}

void DeviceGuitionJC4880P443Portrait::update() {
}

void DeviceGuitionJC4880P443Portrait::pausePpaFor(uint32_t duration_ms) {
  pause_ppa_for(duration_ms);
}

bool DeviceGuitionJC4880P443Portrait::ppaCooldownActive() {
  return ppa_cooldown_active();
}

void DeviceGuitionJC4880P443Portrait::displayPushPixels(int32_t x, int32_t y, int32_t w, int32_t h,
                                                  const uint16_t* data) {
  draw_portrait_area(x, y, w, h, data);
}

void DeviceGuitionJC4880P443Portrait::displayPushPixelsDMA(int32_t x, int32_t y, int32_t w, int32_t h,
                                                     const uint16_t* data) {
  draw_portrait_area(x, y, w, h, data);
}

bool DeviceGuitionJC4880P443Portrait::displayTryFullFramePreview(
    int32_t x, int32_t y, int32_t w, int32_t h,
    int32_t source_stride, const uint16_t* data, size_t data_size,
    bool byte_swap) {
  const p4_dsi_camera_presenter::PpaRuntime runtime{
      g_ppa_handle,
      g_ppa_done,
      g_ppa_async_ready,
      g_ppa_reset_pending,
      ppa_cooldown_active(),
      note_ppa_fault,
      nullptr,
  };
  const bool presented = g_camera_presenter.present(
      x, y, w, h, source_stride, data, data_size, byte_swap, g_rotation,
      runtime);
  if (presented) g_ppa_consecutive_faults = 0;
  return presented;
}

void DeviceGuitionJC4880P443Portrait::displayEndFullFramePreview() {
  g_camera_presenter.end();
}

void DeviceGuitionJC4880P443Portrait::displayWaitDMA() {
}

void DeviceGuitionJC4880P443Portrait::displayCommit() {
  if (!g_panel || !g_panel_fb_ready || !g_frame_dirty) {
    return;
  }

  // Dirty pixels were already written into the active DPI framebuffer.
  reset_dirty_rect();
}

void DeviceGuitionJC4880P443Portrait::displayFillScreen(uint16_t color) {
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
          0, 0, display_cfg.width, display_cfg.height, false);
    }

    reset_dirty_rect();
    return;
  }

  const size_t chunk_pixels = static_cast<size_t>(display_cfg.width) * kFillChunkRows;
  if (!ensure_rotate_buffer(chunk_pixels)) {
    return;
  }

  for (size_t i = 0; i < chunk_pixels; ++i) {
    g_rotate_buf[i] = color;
  }

  for (int32_t y = 0; y < display_cfg.height; y += static_cast<int32_t>(kFillChunkRows)) {
    int32_t rows = static_cast<int32_t>(kFillChunkRows);
    if (y + rows > display_cfg.height) {
      rows = display_cfg.height - y;
    }
    draw_physical(0, y, display_cfg.width, rows, g_rotate_buf);
  }
}

void DeviceGuitionJC4880P443Portrait::displaySetRotation(uint8_t rotation) {
  g_rotation = rotation & 0x03;
  Serial.printf("[Device/GuitionJC4880P443Portrait] Rotation set to %u (%s)\n",
                static_cast<unsigned>(g_rotation),
                (g_rotation & 0x02) ? "180-deg CPU mirror" : "native");
  // Changing the ST7701 scan direction over DCS loses DSI video lock, so the
  // flip is mirrored on the CPU in draw_portrait_area.
}

void DeviceGuitionJC4880P443Portrait::setBrightness(uint8_t value) {
  g_brightness = value;
  apply_backlight(value);
}

uint8_t DeviceGuitionJC4880P443Portrait::getBrightness() {
  return g_brightness;
}

bool DeviceGuitionJC4880P443Portrait::getTouch(int16_t& x, int16_t& y) {
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
      if (g_rotation & 0x02) {
        candidate_x = static_cast<int32_t>(display_cfg.width) - 1 - static_cast<int32_t>(px[i]);
        candidate_y = static_cast<int32_t>(display_cfg.height) - 1 - static_cast<int32_t>(py[i]);
      } else {
        candidate_x = static_cast<int32_t>(px[i]);
        candidate_y = static_cast<int32_t>(py[i]);
      }
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

  if (g_rotation & 0x02) {
    mapped_x = static_cast<int32_t>(display_cfg.width) - 1 - static_cast<int32_t>(px[selected]);
    mapped_y = static_cast<int32_t>(display_cfg.height) - 1 - static_cast<int32_t>(py[selected]);
  } else {
    mapped_x = static_cast<int32_t>(px[selected]);
    mapped_y = static_cast<int32_t>(py[selected]);
  }

  const int32_t logical_w = static_cast<int32_t>(display_cfg.width);
  const int32_t logical_h = static_cast<int32_t>(display_cfg.height);

  if (mapped_x < 0) mapped_x = 0;
  if (mapped_y < 0) mapped_y = 0;
  if (mapped_x >= logical_w) mapped_x = logical_w - 1;
  if (mapped_y >= logical_h) mapped_y = logical_h - 1;

  if (!g_touch_active) {
    Serial.printf("[Device/GuitionJC4880P443Portrait] Touch press raw=(%u,%u) mapped=(%ld,%ld) rotation=%u\n",
                  static_cast<unsigned>(px[selected]),
                  static_cast<unsigned>(py[selected]),
                  static_cast<long>(mapped_x),
                  static_cast<long>(mapped_y),
                  static_cast<unsigned>(g_rotation));
    g_touch_stable_x = mapped_x;
    g_touch_stable_y = mapped_y;
    g_touch_active = true;
  } else {
    const int32_t dx = mapped_x - g_touch_stable_x;
    const int32_t dy = mapped_y - g_touch_stable_y;
    const int32_t adx = dx >= 0 ? dx : -dx;
    const int32_t ady = dy >= 0 ? dy : -dy;

    if (adx <= kTouchJitterThresholdPx && ady <= kTouchJitterThresholdPx) {
      // Ignore tiny GT9xx wobble so taps feel more deterministic.
    } else {
      // GT9271 already applies its own controller-side debounce/filtering.
      // Keep the software path direct so the UI does not feel delayed.
      g_touch_stable_x = mapped_x;
      g_touch_stable_y = mapped_y;
    }
  }

  x = static_cast<int16_t>(g_touch_stable_x);
  y = static_cast<int16_t>(g_touch_stable_y);
  return true;
}

void DeviceGuitionJC4880P443Portrait::displaySleep() {
  if (g_panel) {
    esp_lcd_panel_disp_on_off(g_panel, false);
  }
  apply_backlight(0);
}

void DeviceGuitionJC4880P443Portrait::displayWake() {
  if (g_panel) {
    esp_lcd_panel_disp_on_off(g_panel, true);
  }
  apply_backlight(g_brightness);
}

void DeviceGuitionJC4880P443Portrait::displayWakeDark() {
  if (g_panel) {
    esp_lcd_panel_disp_on_off(g_panel, true);
  }
  apply_backlight(0);
}

void DeviceGuitionJC4880P443Portrait::displayPowerSaveOn() {
  displaySleep();
}

void DeviceGuitionJC4880P443Portrait::displayPowerSaveOff() {
  displayWake();
}

void DeviceGuitionJC4880P443Portrait::displayWaitDisplay() {
}

void DeviceGuitionJC4880P443Portrait::prepareForRestart() {
  if (g_panel) {
    displayFillScreen(0x0000);
    esp_lcd_panel_disp_on_off(g_panel, false);
  }

  hold_panel_reset_low();
  apply_backlight(0);
  ledc_stop(LEDC_LOW_SPEED_MODE, kBacklightChannel, kBacklightActiveLow ? 1u : 0u);
  gpio_set_direction(kBacklightPin, GPIO_MODE_OUTPUT);
  gpio_set_level(kBacklightPin, kBacklightActiveLow ? 1 : 0);
}

bool DeviceGuitionJC4880P443Portrait::initSDCard() {
  if (g_sd_available && GuitionPortraitSDMMC.cardType() != CARD_NONE) {
    return true;
  }

  const uint32_t now = millis();
  if (g_sd_init_attempted && (now - g_sd_retry_tick_ms) < 1500) {
    return false;
  }
  g_sd_init_attempted = true;
  g_sd_retry_tick_ms = now;

  GuitionPortraitSDMMC.end();

  if (!GuitionPortraitSDMMC.begin("/sdcard", false, SDMMC_FREQ_DEFAULT, 5)) {
    g_sd_available = false;
    Serial.println("[Device/GuitionJC4880P443Portrait] SD card mount failed");
    return false;
  }

  const uint8_t card_type = GuitionPortraitSDMMC.cardType();
  if (card_type == CARD_NONE) {
    g_sd_available = false;
    Serial.println("[Device/GuitionJC4880P443Portrait] SD card absent after mount");
    GuitionPortraitSDMMC.end();
    return false;
  }

  g_sd_available = true;
  Serial.printf("[Device/GuitionJC4880P443Portrait] SD card OK, type=%u, size=%llu MB\n",
                static_cast<unsigned>(card_type),
                static_cast<unsigned long long>(GuitionPortraitSDMMC.cardSize() / (1024ULL * 1024ULL)));
  return true;
}

bool DeviceGuitionJC4880P443Portrait::storageReady() {
  return g_littlefs_ready;
}

fs::FS& DeviceGuitionJC4880P443Portrait::storageFS() {
  return LittleFS;
}

bool DeviceGuitionJC4880P443Portrait::sdReady() {
  return initSDCard();
}

fs::FS& DeviceGuitionJC4880P443Portrait::sdFS() {
  return GuitionPortraitSDMMC;
}

bool DeviceGuitionJC4880P443Portrait::suspendSDCardForNetworkTransition() {
  const bool was_mounted =
      g_sd_available && GuitionPortraitSDMMC.cardType() != CARD_NONE;
  if (!was_mounted) return false;

  GuitionPortraitSDMMC.end();
  g_sd_available = false;
  g_sd_init_attempted = false;
  Serial.println("[Device/GuitionJC4880P443Portrait] SD card suspended for network transition");
  return true;
}

bool DeviceGuitionJC4880P443Portrait::resumeSDCardAfterNetworkTransition() {
  g_sd_available = false;
  g_sd_init_attempted = false;
  const bool mounted = initSDCard();
  Serial.printf("[Device/GuitionJC4880P443Portrait] SD card remount after network transition: %s\n",
                mounted ? "OK" : "failed");
  return mounted;
}

bool DeviceGuitionJC4880P443Portrait::initLittleFS() {
  if (g_littlefs_ready) {
    return true;
  }
  if (!LittleFS.begin(true, "/littlefs", 10, "spiffs")) {
    Serial.println("[Device/GuitionJC4880P443Portrait] LittleFS mount failed");
    return false;
  }
  g_littlefs_ready = true;
  Serial.printf("[Device/GuitionJC4880P443Portrait] LittleFS ready, total=%u, used=%u\n",
                static_cast<unsigned>(LittleFS.totalBytes()),
                static_cast<unsigned>(LittleFS.usedBytes()));
  ensure_storage_layout();
  return true;
}

static bool copyFile(fs::FS& srcFS, fs::FS& dstFS, const char* path) {
  File src = srcFS.open(path, FILE_READ);
  if (!src) {
    return false;
  }
  File dst = dstFS.open(path, FILE_WRITE);
  if (!dst) {
    src.close();
    return false;
  }
  uint8_t buf[512];
  while (src.available()) {
    const size_t n = src.read(buf, sizeof(buf));
    if (n == 0) break;
    dst.write(buf, n);
  }
  dst.close();
  src.close();
  return true;
}

static void copyDirectory(fs::FS& srcFS, fs::FS& dstFS, const char* dirPath) {
  File dir = srcFS.open(dirPath);
  if (!dir || !dir.isDirectory()) {
    return;
  }
  dstFS.mkdir(dirPath);
  File entry = dir.openNextFile();
  while (entry) {
    String fullPath = String(dirPath) + "/" + entry.name();
    if (entry.isDirectory()) {
      copyDirectory(srcFS, dstFS, fullPath.c_str());
    } else {
      copyFile(srcFS, dstFS, fullPath.c_str());
      Serial.printf("[Storage] Migrated: %s (%u bytes)\n", fullPath.c_str(),
                    static_cast<unsigned>(entry.size()));
    }
    entry = dir.openNextFile();
  }
}

void DeviceGuitionJC4880P443Portrait::migrateStorageFromSD() {
  if (!initLittleFS()) {
    return;
  }
  if (LittleFS.exists("/_migrated")) {
    return;
  }

  LittleFS.mkdir("/_tile_grids");
  LittleFS.mkdir("/_tile_links");
  LittleFS.mkdir("/icons");

  if (initSDCard()) {
    Serial.println("[Storage] Migrating data from SD to LittleFS...");
    copyDirectory(GuitionPortraitSDMMC, LittleFS, "/_tile_grids");
    copyDirectory(GuitionPortraitSDMMC, LittleFS, "/_tile_links");
    copyDirectory(GuitionPortraitSDMMC, LittleFS, "/icons");
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

#endif  // defined(DEVICE_GUITION_JC4880P443_PORTRAIT)
