#include "src/devices/waveshare_touch_lcd_8/device_waveshare_touch_lcd_8.h"

#include "src/devices/device_select.h"

#if defined(DEVICE_WAVESHARE_TOUCH_LCD_8)

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
#include "src/devices/waveshare_touch_lcd_8/waveshare_sdmmc.h"
#include "src/devices/waveshare_touch_lcd_8/vendor/displays_config.h"
#include "src/devices/waveshare_touch_lcd_8/vendor/gt911.h"
#include "src/devices/waveshare_touch_lcd_8/vendor/i2c.h"
#include "src/devices/waveshare_touch_lcd_8/vendor/jd9365/esp_lcd_jd9365.h"

namespace {

constexpr gpio_num_t kBacklightPin = GPIO_NUM_26;
constexpr ledc_channel_t kBacklightChannel = LEDC_CHANNEL_0;
constexpr ledc_timer_t kBacklightTimer = LEDC_TIMER_1;
constexpr uint32_t kBacklightFreq = 5000;
constexpr ledc_timer_bit_t kBacklightBits = LEDC_TIMER_10_BIT;
constexpr bool kBacklightActiveLow = false;
constexpr uint8_t kBacklightInputMin = 121;
constexpr uint8_t kBacklightInputMax = 255;
constexpr uint32_t kBootBlackWarmupMs = 90;
constexpr uint32_t kBootBlackGapMs = 60;
constexpr uint32_t kPanelLaneCount = 2;
// Two driver-owned framebuffers let the DSI DMA finish scanning the current
// picture before it selects the next one. Normal LVGL rendering still uses one
// active buffer; the second buffer is only kept in lockstep while the camera
// preview is active.
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
// The PPA SRM engine can only rotate WIDE bands out of the fast SRAM draw buffer.
// A narrower partial flush stalls it, and a stalled non-blocking rotate then holds
// the engine's single pending slot ("ppa_srm: exceed maximum pending transactions");
// note_ppa_fault() now heals that, but it still costs a cooldown, so we keep the
// known jammer off the PPA entirely. That jammer is the scrolling media-title band:
// a media tile is capped to 3x3 cells, so its widest possible title is 3*168 + 2*16
// = 536 px (see MEDIA_TILE_MAX_SPAN). The popup interaction bands that DO want the
// PPA are wider — forecast/energy charts are 752 px, popup cards 792 px, full-screen
// repaints 1280 px. So the gate sits between them: above the 536 px capped title
// (keep it on the CPU, it rotates in <1 ms there) and below the 752 px charts (keep
// those on the PPA so popups stay snappy). 600 leaves margin on both sides. History:
// 256 let the title jam the engine (display slow forever); 768 over-corrected and
// pushed the 752 px charts onto the CPU (popups felt sluggish).
constexpr int32_t kPpaMinRotateWidth = 600;
// LVGL scrolling can expose full-width dirty strips only a few rows tall.
// Keep those strips on the CPU because a tiny SRM transaction can be accepted
// without ever producing the completion callback on ESP32-P4.
constexpr int32_t kPpaMinRotateHeight = 8;
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
uint8_t g_rotation = DeviceWaveshareTouchLCD8::kProfile.rotation_default;
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
  Serial.print("[Device/WaveshareTouchLCD8] ");
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
// semaphore we passed via oper.user_data so draw_landscape_area can wake up.
bool IRAM_ATTR on_ppa_trans_done(ppa_client_handle_t, ppa_event_data_t*, void* user_ctx) {
  SemaphoreHandle_t sem = static_cast<SemaphoreHandle_t>(user_ctx);
  if (!sem) {
    return false;
  }

  BaseType_t high_task_woken = pdFALSE;
  xSemaphoreGiveFromISR(sem, &high_task_woken);
  return high_task_woken == pdTRUE;
}

// Tear down a wedged PPA client and bring up a fresh one so the UI heals itself
// instead of staying on the slow CPU rotate until a power cycle. A stalled
// non-blocking rotate keeps its transaction in the client's single pending slot;
// ppa_unregister_client() is non-blocking and refuses while that slot is busy.
// Keep the old handle in that case and retry retiring it after the pending transfer
// has drained. Dropping that handle leaks a client slot and can leave the device on
// CPU-only rotation until reboot.
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
      Serial.printf("[Device/WaveshareTouchLCD8] PPA client busy err=%d, retry reset in %lu ms\n",
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
    Serial.printf("[Device/WaveshareTouchLCD8] PPA reset failed err=%d (int free=%u KB, largest=%u KB), retry in %lu ms\n",
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
    Serial.println("[Device/WaveshareTouchLCD8] PPA timeout callback unavailable, retrying client reset");
    return;
  }
  g_ppa_reinit_at_ms = 0;
  g_ppa_reset_pending = false;
  Serial.println("[Device/WaveshareTouchLCD8] PPA client reset after fault");
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
    Serial.printf("[Device/WaveshareTouchLCD8] draw transfer timeout (%u px)\n",
                  static_cast<unsigned>(pixels));
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
    Serial.printf("[Device/WaveshareTouchLCD8] Rotate buffer allocation failed (%u bytes)\n",
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
    Serial.printf("[Device/WaveshareTouchLCD8] Reject out-of-range draw x=%ld y=%ld w=%ld h=%ld\n",
                  static_cast<long>(x), static_cast<long>(y),
                  static_cast<long>(w), static_cast<long>(h));
    return false;
  }

  drain_transfer_signal();
  flush_cache_for_dma(data, static_cast<size_t>(w) * static_cast<size_t>(h) * sizeof(uint16_t));
  const esp_err_t err = esp_lcd_panel_draw_bitmap(g_panel, x, y, x + w, y + h, data);
  if (err != ESP_OK) {
    Serial.printf("[Device/WaveshareTouchLCD8] draw_bitmap failed: %d\n", static_cast<int>(err));
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
    Serial.printf("[Device/WaveshareTouchLCD8] Panel framebuffer unavailable err=%d fb0=%p fb1=%p\n",
                  static_cast<int>(err), fb0, fb1);
    g_panel_fb_ready = false;
    return false;
  }

  g_panel_fbs[0] = static_cast<uint16_t*>(fb0);
  g_panel_fbs[1] = static_cast<uint16_t*>(fb1);
  const p4_dsi_camera_presenter::Config presenter_config{
      display_cfg.width,
      display_cfg.height,
      p4_dsi_camera_presenter::Transform::Portrait90Or270,
      kPpaRotateTimeoutMs,
      kPpaPreviewGraceMs,
      "WaveshareTouchLCD8",
  };
  g_panel_fb_ready = g_camera_presenter.init(
      presenter_config, g_panel, g_refresh_done, g_panel_fbs[0],
      g_panel_fbs[1]);
  if (!g_panel_fb_ready) {
    Serial.println(
        "[Device/WaveshareTouchLCD8] Shared camera presenter init failed");
    return false;
  }
  reset_dirty_rect();
  Serial.printf("[Device/WaveshareTouchLCD8] Panel framebuffers OK fb0=%p fb1=%p\n",
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
    Serial.printf("[Device/WaveshareTouchLCD8] Reject out-of-range framebuffer draw x=%ld y=%ld w=%ld h=%ld\n",
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

bool draw_landscape_area(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* data) {
  if (!data || w <= 0 || h <= 0) {
    return false;
  }

  const int32_t logical_w = display_cfg.height;
  const int32_t logical_h = display_cfg.width;
  if (x < 0 || y < 0 || (x + w) > logical_w || (y + h) > logical_h) {
    Serial.printf("[Device/WaveshareTouchLCD8] Reject out-of-range landscape draw x=%ld y=%ld w=%ld h=%ld\n",
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

  if (g_ppa_reinit_at_ms &&
      static_cast<int32_t>(millis() - g_ppa_reinit_at_ms) >= 0) {
    g_ppa_reinit_at_ms = 0;
    reset_ppa_client();
  }

  if (g_ppa_handle && g_ppa_async_ready && !g_ppa_reset_pending && g_panel_fb_ready &&
      !ppa_cooldown_active() && w >= kPpaMinRotateWidth &&
      h >= kPpaMinRotateHeight) {
    uint16_t* fb = panel_fb();
    if (fb) {
      flush_cache_for_dma(data, static_cast<size_t>(w) * static_cast<size_t>(h) * sizeof(uint16_t));

      ppa_srm_oper_config_t oper = {};
      oper.in.buffer = data;
      oper.in.pic_w = w;
      oper.in.pic_h = h;
      oper.in.block_w = w;
      oper.in.block_h = h;
      oper.in.block_offset_x = 0;
      oper.in.block_offset_y = 0;
      oper.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;

      oper.out.buffer = fb;
      oper.out.buffer_size = panel_frame_bytes();
      oper.out.pic_w = display_cfg.width;
      oper.out.pic_h = display_cfg.height;
      oper.out.block_offset_x = dst_x;
      oper.out.block_offset_y = dst_y;
      oper.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;

      oper.rotation_angle = (g_rotation & 0x02) ? PPA_SRM_ROTATION_ANGLE_90
                                                : PPA_SRM_ROTATION_ANGLE_270;
      oper.scale_x = 1.0f;
      oper.scale_y = 1.0f;
      oper.rgb_swap = false;
      oper.byte_swap = false;

      // A busy arbiter falls back to CPU rotation before submission. Once IDF
      // accepts a non-blocking transaction, its buffers remain owned until the
      // callback; a missing callback therefore uses the shared fail-closed path.
      bool ppa_ok = false;
      Dma2dArbiterGuard dma2d_guard(25);
      if (g_ppa_async_ready && g_ppa_done && dma2d_guard.locked()) {
        oper.mode = PPA_TRANS_MODE_NON_BLOCKING;
        oper.user_data = g_ppa_done;
        xSemaphoreTake(g_ppa_done, 0);  // drop any late give from a previous timeout
        const esp_err_t err = ppa_do_scale_rotate_mirror(g_ppa_handle, &oper);
        if (err == ESP_OK) {
          if (xSemaphoreTake(g_ppa_done,
                             pdMS_TO_TICKS(kPpaRotateTimeoutMs)) == pdTRUE ||
              xSemaphoreTake(g_ppa_done,
                             pdMS_TO_TICKS(kPpaPreviewGraceMs)) == pdTRUE) {
            ppa_ok = true;
            g_ppa_consecutive_faults = 0;
          } else {
            dma2d_guard.detach();
            p4_dsi_camera_presenter::restartAfterPpaTimeout(
                "WaveshareTouchLCD8", "LVGL PPA rotation", x, y, w, h, w,
                g_rotation, kPpaRotateTimeoutMs + kPpaPreviewGraceMs);
          }
        } else {
          Serial.printf("[Device/WaveshareTouchLCD8] PPA rotate submit failed err=%d -> CPU cooldown\n",
                        static_cast<int>(err));
          note_ppa_fault();
        }
      }

      if (ppa_ok) {
        if (!g_camera_presenter.noteUiWrite(
                dst_x, dst_y, dst_w, dst_h, true)) {
          Serial.println(
              "[Device/WaveshareTouchLCD8] UI framebuffer cache sync failed");
          return false;
        }
        mark_dirty_rect(dst_x, dst_y, dst_w, dst_h);
        return true;
      }
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

  uint32_t scaled = value;
  if (value == 0) {
    scaled = 0;
  } else if (value <= kBacklightInputMin) {
    scaled = 1;
  } else if (value >= kBacklightInputMax) {
    scaled = 255;
  } else {
    const uint32_t span = static_cast<uint32_t>(kBacklightInputMax - kBacklightInputMin);
    scaled = 1u + ((static_cast<uint32_t>(value - kBacklightInputMin) * 254u) + (span / 2u)) / span;
  }

  constexpr uint32_t kMaxDuty = (1u << 10) - 1u;
  uint32_t duty = (scaled * kMaxDuty + 127u) / 255u;
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
    Serial.println("[Device/WaveshareTouchLCD8] Backlight timer init failed");
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
    Serial.println("[Device/WaveshareTouchLCD8] Backlight channel init failed");
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
    Serial.println("[Device/WaveshareTouchLCD8] I2C init failed");
    return false;
  }
  g_i2c_ready = true;
  return true;
}

bool init_pmic() {
  if (g_pmic_ready) {
    return true;
  }

  // The standalone 8-DSI-TOUCH-A panel wiki documents I2C addr 0x45 for
  // backlight control when paired with an ESP32-P4-NANO carrier.
  // The integrated ESP32-P4-WIFI6-Touch-LCD-X BSP instead exposes a dedicated
  // backlight GPIO (GPIO26) and shares I2C with touch/audio devices.
  // Probing 0x45 on this board only adds noise and can send us down the wrong
  // hardware path, so keep PMIC init as a no-op here unless Waveshare publish
  // board-specific proof that the WIFI6 8" variant really needs it.
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
    Serial.printf("[Device/WaveshareTouchLCD8] MIPI PHY LDO init failed err=%d\n",
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
    Serial.println("[Device/WaveshareTouchLCD8] Transfer semaphore allocation failed");
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
  Serial.printf("[Device/WaveshareTouchLCD8] DSI bus init start lane=%u Mbps\n",
                static_cast<unsigned>(display_cfg.lane_bit_rate));
  Serial.flush();
  esp_err_t err = esp_lcd_new_dsi_bus(&bus_cfg, &g_dsi_bus);
  if (err != ESP_OK) {
    Serial.printf("[Device/WaveshareTouchLCD8] DSI bus init failed err=%d\n", static_cast<int>(err));
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
    Serial.printf("[Device/WaveshareTouchLCD8] DSI DBI IO init failed err=%d\n", static_cast<int>(err));
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
  // LVGL flushes are still rotated directly into the currently active DPI
  // framebuffer. The second driver buffer is reserved for tear-free camera
  // swaps; it does not turn ordinary UI rendering into a full double-buffered
  // pipeline.
  dpi_cfg.flags.use_dma2d = true;

  jd9365_vendor_config_t vendor_cfg = {};
  vendor_cfg.init_cmds = nullptr;
  vendor_cfg.init_cmds_size = 0;
  vendor_cfg.mipi_config.dsi_bus = g_dsi_bus;
  vendor_cfg.mipi_config.dpi_config = &dpi_cfg;
  vendor_cfg.mipi_config.lane_num = kPanelLaneCount;

  esp_lcd_panel_dev_config_t panel_cfg = {};
  panel_cfg.reset_gpio_num = display_cfg.lcd_rst;
  panel_cfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
  panel_cfg.data_endian = LCD_RGB_DATA_ENDIAN_LITTLE;
  panel_cfg.bits_per_pixel = 16;
  panel_cfg.flags.reset_active_high = false;
  panel_cfg.vendor_config = &vendor_cfg;

  log_step("JD9365 panel create start");
  err = esp_lcd_new_panel_jd9365(g_panel_io, &panel_cfg, &g_panel);
  if (err != ESP_OK) {
    Serial.printf("[Device/WaveshareTouchLCD8] JD9365 panel create failed err=%d\n", static_cast<int>(err));
    return false;
  }
  log_step("JD9365 panel create OK");

  if (init_panel_framebuffer()) {
    clear_panel_framebuffer(0x0000);
  }

  esp_lcd_dpi_panel_event_callbacks_t cbs = {};
  cbs.on_color_trans_done = on_color_trans_done;
  cbs.on_refresh_done = on_refresh_done;
  log_step("DPI callback register start");
  err = esp_lcd_dpi_panel_register_event_callbacks(g_panel, &cbs, g_transfer_done);
  if (err != ESP_OK) {
    Serial.printf("[Device/WaveshareTouchLCD8] DPI callback register failed err=%d\n", static_cast<int>(err));
    return false;
  }
  log_step("DPI callback register OK");

  log_step("Panel reset start");
  err = esp_lcd_panel_reset(g_panel);
  if (err != ESP_OK) {
    Serial.printf("[Device/WaveshareTouchLCD8] Panel reset failed err=%d\n", static_cast<int>(err));
    return false;
  }
  log_step("Panel reset OK");

  log_step("Panel init start");
  err = esp_lcd_panel_init(g_panel);
  if (err != ESP_OK) {
    Serial.printf("[Device/WaveshareTouchLCD8] Panel init failed err=%d\n", static_cast<int>(err));
    return false;
  }
  log_step("Panel init OK");

  log_step("Panel display on start");
  err = esp_lcd_panel_disp_on_off(g_panel, true);
  if (err != ESP_OK) {
    Serial.printf("[Device/WaveshareTouchLCD8] Panel display on failed err=%d\n", static_cast<int>(err));
    return false;
  }
  log_step("Panel display on OK");

  ppa_client_config_t ppa_cfg = {};
  ppa_cfg.oper_type = PPA_OPERATION_SRM;
  const esp_err_t ppa_err = ppa_register_client(&ppa_cfg, &g_ppa_handle);
  if (ppa_err != ESP_OK) {
    Serial.printf("[Device/WaveshareTouchLCD8] PPA client register failed err=%d, falling back to CPU rotate\n",
                  static_cast<int>(ppa_err));
    g_ppa_handle = nullptr;
    g_ppa_reset_pending = true;
    g_ppa_reinit_at_ms = millis() + kPpaReinitRetryMs;
    if (!g_ppa_reinit_at_ms) g_ppa_reinit_at_ms = 1;
  } else {
    g_ppa_reset_pending = false;
    log_step("PPA client registered");
    // Arm the done-callback so draw_landscape_area can use the timeout-safe
    // non-blocking path. If this fails, stay on the bounded CPU fallback and
    // retry the client instead of entering an unbounded blocking PPA call.
    g_ppa_done = xSemaphoreCreateBinary();
    if (g_ppa_done) {
      ppa_event_callbacks_t ppa_cbs = {};
      ppa_cbs.on_trans_done = on_ppa_trans_done;
      if (ppa_client_register_event_callbacks(g_ppa_handle, &ppa_cbs) == ESP_OK) {
        g_ppa_async_ready = true;
        log_step("PPA timeout-safe mode ready");
      } else {
        Serial.println("[Device/WaveshareTouchLCD8] PPA event cb register failed, retrying safely");
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
    Serial.printf("[Device/WaveshareTouchLCD8] Touch init attempt %u failed\n",
                  static_cast<unsigned>(attempt));
    delay(150);
  }

  if (!g_touch) {
    Serial.println("[Device/WaveshareTouchLCD8] Touch init failed");
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

}  // namespace

bool DeviceWaveshareTouchLCD8::init() {
  Serial.println("[Device/WaveshareTouchLCD8] Initialising board...");

  if (!init_backlight()) {
    return false;
  }
  apply_backlight(0);

  if (!init_display()) {
    return false;
  }
  Serial.println("[Device/WaveshareTouchLCD8] Display OK");

  if (!init_touch()) {
    Serial.println("[Device/WaveshareTouchLCD8] Touch init failed, continuing without touch");
  } else {
    Serial.println("[Device/WaveshareTouchLCD8] Touch OK");
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
  Serial.println("[Device/WaveshareTouchLCD8] Init complete");
  return true;
}

void DeviceWaveshareTouchLCD8::update() {
}

void DeviceWaveshareTouchLCD8::pausePpaFor(uint32_t duration_ms) {
  pause_ppa_for(duration_ms);
}

bool DeviceWaveshareTouchLCD8::ppaCooldownActive() {
  return ppa_cooldown_active();
}

void DeviceWaveshareTouchLCD8::displayPushPixels(int32_t x, int32_t y, int32_t w, int32_t h,
                                                 const uint16_t* data) {
  draw_landscape_area(x, y, w, h, data);
}

void DeviceWaveshareTouchLCD8::displayPushPixelsDMA(int32_t x, int32_t y, int32_t w, int32_t h,
                                                    const uint16_t* data) {
  draw_landscape_area(x, y, w, h, data);
}

bool DeviceWaveshareTouchLCD8::displayTryFullFramePreview(
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

void DeviceWaveshareTouchLCD8::displayWaitDMA() {
}

void DeviceWaveshareTouchLCD8::displayWaitFrameStart() {
  // Camera presentation uses an inactive framebuffer and swaps only at the
  // following frame boundary. Waiting before the PPA copy is therefore neither
  // necessary nor useful.
}

void DeviceWaveshareTouchLCD8::displayEndFullFramePreview() {
  g_camera_presenter.end();
}

void DeviceWaveshareTouchLCD8::displayCommit() {
  if (!g_panel || !g_panel_fb_ready || !g_frame_dirty) {
    return;
  }

  // Dirty pixels were already written into the active DPI framebuffer.
  reset_dirty_rect();
}

void DeviceWaveshareTouchLCD8::displayFillScreen(uint16_t color) {
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

void DeviceWaveshareTouchLCD8::displaySetRotation(uint8_t rotation) {
  g_rotation = rotation & 0x03;
}

void DeviceWaveshareTouchLCD8::setBrightness(uint8_t value) {
  g_brightness = value;
  apply_backlight(value);
}

uint8_t DeviceWaveshareTouchLCD8::getBrightness() {
  return g_brightness;
}

bool DeviceWaveshareTouchLCD8::getTouch(int16_t& x, int16_t& y) {
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
        candidate_x = static_cast<int32_t>(display_cfg.height) - 1 - static_cast<int32_t>(py[i]);
        candidate_y = static_cast<int32_t>(px[i]);
      } else {
        candidate_x = static_cast<int32_t>(py[i]);
        candidate_y = static_cast<int32_t>(display_cfg.width) - 1 - static_cast<int32_t>(px[i]);
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
    mapped_x = static_cast<int32_t>(display_cfg.height) - 1 - static_cast<int32_t>(py[selected]);
    mapped_y = static_cast<int32_t>(px[selected]);
  } else {
    mapped_x = static_cast<int32_t>(py[selected]);
    mapped_y = static_cast<int32_t>(display_cfg.width) - 1 - static_cast<int32_t>(px[selected]);
  }

  const int32_t logical_w = static_cast<int32_t>(display_cfg.height);
  const int32_t logical_h = static_cast<int32_t>(display_cfg.width);

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

void DeviceWaveshareTouchLCD8::displaySleep() {
  if (g_panel) {
    esp_lcd_panel_disp_on_off(g_panel, false);
  }
  apply_backlight(0);
}

void DeviceWaveshareTouchLCD8::displayWake() {
  if (g_panel) {
    esp_lcd_panel_disp_on_off(g_panel, true);
  }
  apply_backlight(g_brightness);
}

void DeviceWaveshareTouchLCD8::displayWakeDark() {
  if (g_panel) {
    esp_lcd_panel_disp_on_off(g_panel, true);
  }
  apply_backlight(0);
}

void DeviceWaveshareTouchLCD8::displayPowerSaveOn() {
  displaySleep();
}

void DeviceWaveshareTouchLCD8::displayPowerSaveOff() {
  displayWake();
}

void DeviceWaveshareTouchLCD8::displayWaitDisplay() {
}

void DeviceWaveshareTouchLCD8::prepareForRestart() {
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

i2c_master_bus_handle_t DeviceWaveshareTouchLCD8::sharedI2cBus() {
  return g_i2c_ready ? g_i2c.bus : nullptr;
}

bool DeviceWaveshareTouchLCD8::initSDCard() {
  if (g_sd_available && WaveshareSDMMC.cardType() != CARD_NONE) {
    return true;
  }

  const uint32_t now = millis();
  if (g_sd_init_attempted && (now - g_sd_retry_tick_ms) < 1500) {
    return false;
  }
  g_sd_init_attempted = true;
  g_sd_retry_tick_ms = now;

  WaveshareSDMMC.end();

  if (!WaveshareSDMMC.begin("/sdcard", false, SDMMC_FREQ_DEFAULT, 5)) {
    g_sd_available = false;
    Serial.println("[Device/WaveshareTouchLCD8] SD card mount failed");
    return false;
  }

  const uint8_t card_type = WaveshareSDMMC.cardType();
  if (card_type == CARD_NONE) {
    g_sd_available = false;
    Serial.println("[Device/WaveshareTouchLCD8] SD card absent after mount");
    WaveshareSDMMC.end();
    return false;
  }

  g_sd_available = true;
  Serial.printf("[Device/WaveshareTouchLCD8] SD card OK, type=%u, size=%llu MB\n",
                static_cast<unsigned>(card_type),
                static_cast<unsigned long long>(WaveshareSDMMC.cardSize() / (1024ULL * 1024ULL)));
  return true;
}

bool DeviceWaveshareTouchLCD8::storageReady() {
  return g_littlefs_ready;
}

fs::FS& DeviceWaveshareTouchLCD8::storageFS() {
  return LittleFS;
}

bool DeviceWaveshareTouchLCD8::sdReady() {
  return initSDCard();
}

fs::FS& DeviceWaveshareTouchLCD8::sdFS() {
  return WaveshareSDMMC;
}

bool DeviceWaveshareTouchLCD8::suspendSDCardForNetworkTransition() {
  const bool was_mounted =
      g_sd_available && WaveshareSDMMC.cardType() != CARD_NONE;
  if (!was_mounted) return false;

  WaveshareSDMMC.end();
  g_sd_available = false;
  g_sd_init_attempted = false;
  Serial.println("[Device/WaveshareTouchLCD8] SD card suspended for network transition");
  return true;
}

bool DeviceWaveshareTouchLCD8::resumeSDCardAfterNetworkTransition() {
  g_sd_available = false;
  g_sd_init_attempted = false;
  const bool mounted = initSDCard();
  Serial.printf("[Device/WaveshareTouchLCD8] SD card remount after network transition: %s\n",
                mounted ? "OK" : "failed");
  return mounted;
}

bool DeviceWaveshareTouchLCD8::initLittleFS() {
  if (g_littlefs_ready) {
    return true;
  }
  if (!LittleFS.begin(true, "/littlefs", 10, "spiffs")) {
    Serial.println("[Device/WaveshareTouchLCD8] LittleFS mount failed");
    return false;
  }
  g_littlefs_ready = true;
  Serial.printf("[Device/WaveshareTouchLCD8] LittleFS ready, total=%u, used=%u\n",
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

void DeviceWaveshareTouchLCD8::migrateStorageFromSD() {
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
    copyDirectory(WaveshareSDMMC, LittleFS, "/_tile_grids");
    copyDirectory(WaveshareSDMMC, LittleFS, "/_tile_links");
    copyDirectory(WaveshareSDMMC, LittleFS, "/icons");
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

#endif  // defined(DEVICE_WAVESHARE_TOUCH_LCD_8)
