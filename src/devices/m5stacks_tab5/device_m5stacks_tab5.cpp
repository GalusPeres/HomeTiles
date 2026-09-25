#include "src/devices/m5stacks_tab5/device_m5stacks_tab5.h"

#include "src/devices/device_select.h"

#if defined(DEVICE_M5STACKS_TAB5)

#include <driver/ppa.h>
#include <esp_cache.h>
#include <esp_heap_caps.h>
#include <soc/ppa_reg.h>

#include "src/core/display/dma2d_arbiter.h"
#include "src/devices/common/p4_dsi_camera_presenter.h"
#include <LittleFS.h>
#include <M5Unified.h>
#include <SD.h>
#include <SPI.h>
#include <Wire.h>
#include <lgfx/v1/platforms/esp32p4/Panel_DSI.hpp>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace {

constexpr int32_t kLogicalWidth = DeviceM5StacksTab5::kProfile.screen_width;
constexpr int32_t kLogicalHeight = DeviceM5StacksTab5::kProfile.screen_height;
constexpr int32_t kPanelWidth = 720;
constexpr int32_t kPanelHeight = 1280;
constexpr size_t kPanelFrameBytes =
    static_cast<size_t>(kPanelWidth) * static_cast<size_t>(kPanelHeight) * sizeof(uint16_t);
constexpr size_t kCacheLineSize = 64;
// Use PPA only for wide repaints, such as swipes and fullscreen updates.
// CPU rotation of small rectangles costs less than transaction overhead;
// fewer transactions also reduce the risk of wedging the pool. The 600-pixel
// threshold has been empirically stable on the 8-inch device since v0.4.11.
constexpr int32_t kPpaMinRotateWidth = 600;
constexpr int32_t kPpaMinRotateHeight = 8;   // Thin strips may wedge 2D-DMA; the driver has no minimum-height check.
constexpr uint32_t kPpaRotateTimeoutMs = 80;
constexpr uint32_t kPpaWedgeGraceMs = 400;   // Grace period to distinguish bus load from a wedged engine.
constexpr uint32_t kPpaFaultCooldownMs = 500;
constexpr uint32_t kPpaReinitRetryMs = 3000;

constexpr int kSdClkPin = 43;
constexpr int kSdMisoPin = 39;
constexpr int kSdMosiPin = 44;
constexpr int kSdCsPin = 42;
constexpr uint32_t kSdClockHz = 25000000;

bool g_display_ready = false;
bool g_sd_init_attempted = false;
bool g_sd_available = false;
uint32_t g_sd_retry_tick_ms = 0;
bool g_littlefs_ready = false;
uint8_t g_brightness = 150;
uint8_t g_rotation = DeviceM5StacksTab5::kProfile.rotation_default;

// The Tab5 backlight driver keeps running when the level is lowered to 1%, but
// it does not start from off at such a low duty: waking at 1% left the display
// dark. Switching on from off therefore starts at a safe level for a moment
// and then settles on the requested value.
constexpr uint8_t kBacklightStartLevel = 64;
constexpr uint32_t kBacklightStartHoldMs = 20;
uint8_t g_backlight_applied = 0;

void apply_backlight(uint8_t value) {
  if (g_backlight_applied == 0 && value > 0 && value < kBacklightStartLevel) {
    M5.Display.setBrightness(kBacklightStartLevel);
    delay(kBacklightStartHoldMs);
  }
  M5.Display.setBrightness(value);
  g_backlight_applied = value;
}
ppa_client_handle_t g_ppa_handle = nullptr;
SemaphoreHandle_t g_ppa_done = nullptr;
bool g_ppa_async_ready = false;
bool g_ppa_ready = false;
uint16_t* g_panel_fb = nullptr;
uint8_t g_ppa_consecutive_faults = 0;
uint32_t g_ppa_cooldown_until_ms = 0;
uint32_t g_ppa_fallback_log_count = 0;
uint32_t g_ppa_fallback_log_window_ms = 0;
uint32_t g_ppa_reinit_at_ms = 0;  // 0 = no reinitialization needed.
// Known IDF gap (TODO in ppa_core.c): an SRM transaction can block 2D-DMA,
// making unregister fail with "client still has unprocessed trans". Previously,
// PPA then stayed disabled until reboot. Under bus load from hardware JPEG
// covers, PixelAnim and DSI scanout, transactions often finish well after the
// 480-ms deadline. Report the wedge, continue CPU drawing and listen for the
// late completion before safely recreating the client.
bool g_ppa_wedged = false;
uint32_t g_ppa_wedged_since_ms = 0;
uint32_t g_ppa_wedge_retry_at_ms = 0;
uint32_t g_ppa_wedge_retry_delay_ms = 0;
uint8_t g_ppa_wedge_reset_attempts = 0;
constexpr uint32_t kPpaWedgeResetRetryMs = 5000;      // First retry delay; doubles thereafter.
constexpr uint32_t kPpaWedgeResetRetryMaxMs = 60000;  // Maximum backoff.
constexpr uint8_t kPpaWedgeResetMaxAttempts = 8;      // Afterwards, only listen silently.

uint8_t to_panel_rotation(uint8_t logical_rotation) {
  logical_rotation &= 0x03;
  return (logical_rotation & 0x02) ? 3 : 1;
}

bool IRAM_ATTR on_ppa_trans_done(ppa_client_handle_t, ppa_event_data_t*, void* user_ctx) {
  SemaphoreHandle_t sem = static_cast<SemaphoreHandle_t>(user_ctx);
  if (!sem) {
    return false;
  }

  BaseType_t high_task_woken = pdFALSE;
  xSemaphoreGiveFromISR(sem, &high_task_woken);
  return high_task_woken == pdTRUE;
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

bool ppa_cooldown_active() {
  if (!g_ppa_cooldown_until_ms) {
    return false;
  }
  const uint32_t now = millis();
  if (static_cast<int32_t>(g_ppa_cooldown_until_ms - now) > 0) {
    return true;
  }
  g_ppa_cooldown_until_ms = 0;
  return false;
}

void pause_ppa_for(uint32_t duration_ms) {
  g_ppa_cooldown_until_ms = millis() + duration_ms;
}

// true means a fresh client is ready. If unregister fails because the sole
// pending slot still holds a transaction, retain the old handle and retry
// later. Discarding it would leak the client slot and heap, as with the
// 8-inch device's recovery pattern since v0.4.11.
bool reset_ppa_client() {
  g_ppa_ready = false;
  g_ppa_async_ready = false;

  if (g_ppa_handle) {
    const esp_err_t unreg_err = ppa_unregister_client(g_ppa_handle);
    if (unreg_err != ESP_OK) {
      Serial.printf("[Device/M5StacksTab5] PPA unregister rejected err=%d (transaction open), retry follows\n",
                    static_cast<int>(unreg_err));
      return false;
    }
    g_ppa_handle = nullptr;
  }

  ppa_client_config_t ppa_cfg = {};
  ppa_cfg.oper_type = PPA_OPERATION_SRM;
  ppa_client_handle_t fresh = nullptr;
  const esp_err_t reg_err = ppa_register_client(&ppa_cfg, &fresh);
  if (reg_err != ESP_OK || !fresh) {
    // Retry automatically later so the device does not remain in the slower
    // M5GFX fallback until reboot.
    g_ppa_reinit_at_ms = millis() + kPpaReinitRetryMs;
    if (!g_ppa_reinit_at_ms) g_ppa_reinit_at_ms = 1;
    Serial.printf("[Device/M5StacksTab5] PPA reset failed err=%d (int free=%u KB, largest=%u KB), retry in %lu ms\n",
                  static_cast<int>(reg_err),
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                  static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024),
                  static_cast<unsigned long>(kPpaReinitRetryMs));
    return false;
  }

  if (!g_ppa_done) g_ppa_done = xSemaphoreCreateBinary();
  esp_err_t callback_err = ESP_ERR_NO_MEM;
  if (g_ppa_done) {
    ppa_event_callbacks_t ppa_cbs = {};
    ppa_cbs.on_trans_done = on_ppa_trans_done;
    callback_err = ppa_client_register_event_callbacks(fresh, &ppa_cbs);
    g_ppa_async_ready = callback_err == ESP_OK;
  }
  if (!g_ppa_async_ready) {
    const esp_err_t unreg_err = ppa_unregister_client(fresh);
    if (unreg_err != ESP_OK) g_ppa_handle = fresh;
    g_ppa_reinit_at_ms = millis() + kPpaReinitRetryMs;
    if (!g_ppa_reinit_at_ms) g_ppa_reinit_at_ms = 1;
    Serial.printf(
        "[Device/M5StacksTab5] PPA async setup failed err=%d; using "
        "M5GFX fallback\n",
        static_cast<int>(callback_err));
    return false;
  }

  g_ppa_handle = fresh;
  g_ppa_ready = (g_panel_fb != nullptr);
  g_ppa_reinit_at_ms = 0;
  Serial.println("[Device/M5StacksTab5] PPA client reset");
  return g_ppa_ready;
}

void note_ppa_fault() {
  // Do not reset the client here: ppa_unregister_client cannot succeed with
  // a pending, wedged transaction. The rotation path detects g_ppa_wedged and
  // recovers when the late completion arrives.
  Serial.printf("[Device/M5StacksTab5] PPA fault #%u (int free=%u KB, largest=%u KB)\n",
                static_cast<unsigned>(g_ppa_consecutive_faults + 1),
                static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024));
  ++g_ppa_consecutive_faults;
  pause_ppa_for(kPpaFaultCooldownMs);
}

void log_ppa_fallback(const char* reason, int err = 0) {
  // Renew the log budget every 30 seconds so later failures, such as those
  // first triggered in the Wi-Fi menu, remain visible.
  const uint32_t now = millis();
  if (now - g_ppa_fallback_log_window_ms > 30000) {
    g_ppa_fallback_log_window_ms = now;
    g_ppa_fallback_log_count = 0;
  }
  if (g_ppa_fallback_log_count >= 6) {
    return;
  }
  ++g_ppa_fallback_log_count;
  if (err) {
    Serial.printf("[Device/M5StacksTab5] PPA fallback: %s err=%d\n", reason, err);
  } else {
    Serial.printf("[Device/M5StacksTab5] PPA fallback: %s\n", reason);
  }
}

bool init_tab5_ppa() {
  if (g_ppa_ready) {
    return true;
  }
  if (M5.getBoard() != m5::board_t::board_M5Tab5) {
    Serial.println("[Device/M5StacksTab5] PPA skipped: detected board is not Tab5");
    return false;
  }

  auto* panel = static_cast<lgfx::Panel_DSI*>(M5.Display.getPanel());
  if (!panel) {
    Serial.println("[Device/M5StacksTab5] PPA skipped: no DSI panel");
    return false;
  }

  g_panel_fb = static_cast<uint16_t*>(panel->config_detail().buffer);
  if (!g_panel_fb) {
    Serial.println("[Device/M5StacksTab5] PPA skipped: no panel framebuffer");
    return false;
  }

  ppa_client_config_t ppa_cfg = {};
  ppa_cfg.oper_type = PPA_OPERATION_SRM;
  const esp_err_t ppa_err = ppa_register_client(&ppa_cfg, &g_ppa_handle);
  if (ppa_err != ESP_OK || !g_ppa_handle) {
    Serial.printf("[Device/M5StacksTab5] PPA client register failed err=%d, using M5GFX fallback\n",
                  static_cast<int>(ppa_err));
    g_ppa_handle = nullptr;
    g_ppa_reinit_at_ms = millis() + kPpaReinitRetryMs;
    if (!g_ppa_reinit_at_ms) g_ppa_reinit_at_ms = 1;
    return false;
  }

  if (!g_ppa_done) g_ppa_done = xSemaphoreCreateBinary();
  if (g_ppa_done) {
    ppa_event_callbacks_t ppa_cbs = {};
    ppa_cbs.on_trans_done = on_ppa_trans_done;
    if (ppa_client_register_event_callbacks(g_ppa_handle, &ppa_cbs) == ESP_OK) {
      g_ppa_async_ready = true;
    }
  }

  if (!g_ppa_async_ready) {
    const esp_err_t unreg_err = ppa_unregister_client(g_ppa_handle);
    if (unreg_err == ESP_OK) g_ppa_handle = nullptr;
    g_ppa_reinit_at_ms = millis() + kPpaReinitRetryMs;
    if (!g_ppa_reinit_at_ms) g_ppa_reinit_at_ms = 1;
    Serial.println(
        "[Device/M5StacksTab5] PPA async callback unavailable; using "
        "M5GFX fallback");
    return false;
  }

  g_ppa_ready = true;
  Serial.printf("[Device/M5StacksTab5] PPA ready: fb=%p panel=%ldx%ld async=%u\n",
                g_panel_fb, static_cast<long>(kPanelWidth), static_cast<long>(kPanelHeight),
                static_cast<unsigned>(g_ppa_async_ready ? 1 : 0));
  return true;
}

bool init_display() {
  if (g_display_ready) {
    return true;
  }

  Serial.println("[Device/M5StacksTab5] Display: M5.begin()...");
  Serial.flush();

  auto cfg = M5.config();
  // M5Unified remembers the current M5GFX brightness before init and restores
  // it at the end of begin(). Force that saved value to 0 to avoid a short
  // pre-splash backlight blink on Tab5.
  M5.Display.setBrightness(0);
  M5.begin(cfg);
  M5.Display.setBrightness(0);
  Wire.setClock(400000);

  if (M5.getBoard() == m5::board_t::board_unknown) {
    Serial.println("[Device/M5StacksTab5] M5.begin() failed to detect board");
    Serial.flush();
    return false;
  }

  M5.Display.setRotation(to_panel_rotation(g_rotation));
  M5.Display.setBrightness(0);
  M5.Display.fillScreen(0x0000);
  M5.Display.waitDMA();

  g_display_ready = true;
  init_tab5_ppa();

  Serial.printf("[Device/M5StacksTab5] Display ready: %dx%d rot=%u\n",
                M5.Display.width(), M5.Display.height(),
                static_cast<unsigned>(to_panel_rotation(g_rotation)));
  Serial.flush();
  return true;
}

bool rect_inside_logical_bounds(int32_t x, int32_t y, int32_t w, int32_t h) {
  return x >= 0 && y >= 0 && w > 0 && h > 0 &&
         (x + w) <= kLogicalWidth &&
         (y + h) <= kLogicalHeight;
}

bool ppa_rotate_to_panel(int32_t x, int32_t y, int32_t w, int32_t h,
                         int32_t source_stride, const uint16_t* data,
                         bool byte_swap) {
  if (g_ppa_wedged) {
    // A late completion means the engine is free and the client can be
    // recreated safely. Keep listening indefinitely at no additional cost.
    if (g_ppa_done && xSemaphoreTake(g_ppa_done, 0) == pdTRUE) {
      Serial.printf("[Device/M5StacksTab5] PPA wedge cleared (transaction completed after %lu ms), client reset\n",
                    static_cast<unsigned long>(millis() - g_ppa_wedged_since_ms));
      g_ppa_wedged = false;
      g_ppa_consecutive_faults = 0;
      reset_ppa_client();
      pause_ppa_for(kPpaFaultCooldownMs);
    } else if (g_ppa_wedge_reset_attempts < kPpaWedgeResetMaxAttempts &&
               static_cast<int32_t>(millis() - g_ppa_wedge_retry_at_ms) >= 0) {
      // Without a completion notification, reset succeeds only if the
      // transaction finished silently. Each attempt produces an IDF error,
      // so back off from 5s to 10s up to 60s and stop after
      // kPpaWedgeResetMaxAttempts. The silent semaphore check above continues
      // to allow recovery.
      ++g_ppa_wedge_reset_attempts;
      g_ppa_wedge_retry_delay_ms *= 2;
      if (g_ppa_wedge_retry_delay_ms > kPpaWedgeResetRetryMaxMs) {
        g_ppa_wedge_retry_delay_ms = kPpaWedgeResetRetryMaxMs;
      }
      g_ppa_wedge_retry_at_ms = millis() + g_ppa_wedge_retry_delay_ms;
      if (reset_ppa_client()) {
        Serial.println("[Device/M5StacksTab5] PPA wedge cleared (client reset successful)");
        g_ppa_wedged = false;
        g_ppa_consecutive_faults = 0;
        pause_ppa_for(kPpaFaultCooldownMs);
      } else if (g_ppa_wedge_reset_attempts >= kPpaWedgeResetMaxAttempts) {
        Serial.printf("[Device/M5StacksTab5] PPA reset attempts exhausted — CPU mode until late completion or restart (int_raw=0x%08lx err_st=0x%08lx)\n",
                      static_cast<unsigned long>(REG_READ(PPA_INT_RAW_REG)),
                      static_cast<unsigned long>(REG_READ(PPA_SR_PARAM_ERR_ST_REG)));
      }
    }
    return false;
  }
  if (!g_ppa_ready || !g_ppa_handle) {
    // After a failed reset, schedule a retry instead of staying permanently
    // in the slower fallback.
    if (g_ppa_reinit_at_ms && g_panel_fb &&
        static_cast<int32_t>(millis() - g_ppa_reinit_at_ms) >= 0) {
      g_ppa_reinit_at_ms = 0;
      reset_ppa_client();
    }
    if (!g_ppa_ready || !g_ppa_handle) {
      return false;
    }
  }
  if (!g_panel_fb || source_stride < w || ppa_cooldown_active() ||
      w < kPpaMinRotateWidth ||
      h < kPpaMinRotateHeight) {
    return false;
  }

  int32_t dst_x = 0;
  int32_t dst_y = 0;
  const int32_t dst_w = h;
  const int32_t dst_h = w;
  if (g_rotation & 0x02) {
    dst_x = y;
    dst_y = kLogicalWidth - x - w;
  } else {
    dst_x = kLogicalHeight - y - h;
    dst_y = x;
  }

  if (dst_x < 0 || dst_y < 0 || (dst_x + dst_w) > kPanelWidth ||
      (dst_y + dst_h) > kPanelHeight) {
    log_ppa_fallback("mapped rect outside panel");
    return false;
  }

  // Never overlap hardware JPEG decoding: it shares the 2D-DMA pool
  // (see dma2d_arbiter.h), and overlapping use is the main suspect for lost
  // transactions. If the lock is not available in time, CPU-render this rectangle.
  Dma2dArbiterGuard dma2d_guard(250);
  if (!dma2d_guard.locked()) {
    log_ppa_fallback("2D DMA arbiter busy");
    return false;
  }

  const size_t source_bytes =
      ((static_cast<size_t>(h - 1) * static_cast<size_t>(source_stride)) +
       static_cast<size_t>(w)) * sizeof(uint16_t);
  flush_cache_for_dma(data, source_bytes);

  ppa_srm_oper_config_t oper = {};
  oper.in.buffer = data;
  oper.in.pic_w = source_stride;
  oper.in.pic_h = h;
  oper.in.block_w = w;
  oper.in.block_h = h;
  oper.in.block_offset_x = 0;
  oper.in.block_offset_y = 0;
  oper.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;

  oper.out.buffer = g_panel_fb;
  oper.out.buffer_size = kPanelFrameBytes;
  oper.out.pic_w = kPanelWidth;
  oper.out.pic_h = kPanelHeight;
  oper.out.block_offset_x = dst_x;
  oper.out.block_offset_y = dst_y;
  oper.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;

  oper.rotation_angle = (g_rotation & 0x02) ? PPA_SRM_ROTATION_ANGLE_90
                                            : PPA_SRM_ROTATION_ANGLE_270;
  oper.scale_x = 1.0f;
  oper.scale_y = 1.0f;
  oper.rgb_swap = false;
  oper.byte_swap = byte_swap;

  if (!g_ppa_async_ready || !g_ppa_done) return false;
  oper.mode = PPA_TRANS_MODE_NON_BLOCKING;
  oper.user_data = g_ppa_done;
  xSemaphoreTake(g_ppa_done, 0);
  const esp_err_t err = ppa_do_scale_rotate_mirror(g_ppa_handle, &oper);
  if (err != ESP_OK) {
    log_ppa_fallback("submit failed", static_cast<int>(err));
    Serial.printf("[Device/M5StacksTab5] PPA submit-geo: %ldx%ld src=(%ld,%ld) dst=(%ld,%ld)\n",
                  static_cast<long>(w), static_cast<long>(h),
                  static_cast<long>(x), static_cast<long>(y),
                  static_cast<long>(dst_x), static_cast<long>(dst_y));
    note_ppa_fault();
    return false;
  }
  if (xSemaphoreTake(g_ppa_done, pdMS_TO_TICKS(kPpaRotateTimeoutMs)) != pdTRUE) {
    // Allow a bounded grace period under heavy PSRAM, DSI and Wi-Fi load.
    if (xSemaphoreTake(g_ppa_done, pdMS_TO_TICKS(kPpaWedgeGraceMs)) == pdTRUE) {
      Serial.printf("[Device/M5StacksTab5] PPA slow (>%lu ms): %ldx%ld dst=(%ld,%ld)\n",
                    static_cast<unsigned long>(kPpaRotateTimeoutMs),
                    static_cast<long>(w), static_cast<long>(h),
                    static_cast<long>(dst_x), static_cast<long>(dst_y));
      g_ppa_consecutive_faults = 0;
      return true;
    }
    // IDF 5.5 cannot cancel a submitted SRM transaction. Returning here
    // would let M5GFX and JPEG reuse buffers while PPA may still own them.
    dma2d_guard.detach();
    p4_dsi_camera_presenter::restartAfterPpaTimeout(
        "M5StacksTab5", "M5GFX/LVGL PPA rotation", x, y, w, h,
        source_stride, g_rotation,
        kPpaRotateTimeoutMs + kPpaWedgeGraceMs);
  }
  g_ppa_consecutive_faults = 0;
  return true;
}

void push_pixels_with_ppa_fallback(int32_t x, int32_t y, int32_t w, int32_t h,
                                   const uint16_t* data, bool dma) {
  // Tab5 LVGL uses RGB565_SWAPPED; the panel framebuffer uses RGB565.
  if (ppa_rotate_to_panel(x, y, w, h, w, data, true)) {
    return;
  }

  if (dma) {
    M5.Display.pushImageDMA(x, y, w, h, data);
  } else {
    M5.Display.pushImage(x, y, w, h, data);
  }
}

}  // namespace

bool DeviceM5StacksTab5::init() {
  if (g_display_ready) {
    return true;
  }

  Serial.println("[Device/M5StacksTab5] Initialising board...");
  Serial.flush();

  if (!init_display()) {
    return false;
  }

  M5.Display.setBrightness(0);

  Serial.println("[Device/M5StacksTab5] Init complete");
  Serial.flush();
  return true;
}

void DeviceM5StacksTab5::update() {
  if (!g_display_ready) {
    return;
  }

  M5.update();
}

void DeviceM5StacksTab5::displayPushPixels(int32_t x, int32_t y, int32_t w, int32_t h,
                                          const uint16_t* data) {
  if (!g_display_ready || !data || !rect_inside_logical_bounds(x, y, w, h)) {
    return;
  }

  push_pixels_with_ppa_fallback(x, y, w, h, data, false);
}

void DeviceM5StacksTab5::displayPushPixelsDMA(int32_t x, int32_t y, int32_t w, int32_t h,
                                             const uint16_t* data) {
  if (!g_display_ready || !data || !rect_inside_logical_bounds(x, y, w, h)) {
    return;
  }

  push_pixels_with_ppa_fallback(x, y, w, h, data, true);
}

bool DeviceM5StacksTab5::displayTryFullFramePreview(
    int32_t x, int32_t y, int32_t w, int32_t h,
    int32_t source_stride, const uint16_t* data, size_t data_size,
    bool byte_swap) {
  if (!g_display_ready || !data || !rect_inside_logical_bounds(x, y, w, h) ||
      !g_ppa_async_ready || !g_ppa_done ||
      source_stride < w ||
      (reinterpret_cast<uintptr_t>(data) & (kCacheLineSize - 1)) != 0) {
    return false;
  }
  const size_t required_bytes =
      ((static_cast<size_t>(h - 1) * static_cast<size_t>(source_stride)) +
       static_cast<size_t>(w)) * sizeof(uint16_t);
  if (data_size < required_bytes) return false;

  return ppa_rotate_to_panel(
      x, y, w, h, source_stride, data, byte_swap);
}

void DeviceM5StacksTab5::displayWaitDMA() {
  if (!g_display_ready) {
    return;
  }

  M5.Display.waitDMA();
}

void DeviceM5StacksTab5::displayFillScreen(uint16_t color) {
  if (!g_display_ready) {
    return;
  }

  M5.Display.waitDMA();
  M5.Display.fillScreen(color);
  M5.Display.waitDMA();
}

void DeviceM5StacksTab5::displaySetRotation(uint8_t rotation) {
  g_rotation = rotation & 0x03;

  if (!g_display_ready) {
    return;
  }

  M5.Display.waitDMA();
  M5.Display.setRotation(to_panel_rotation(g_rotation));
}

void DeviceM5StacksTab5::setBrightness(uint8_t value) {
  g_brightness = value;

  if (!g_display_ready) {
    return;
  }

  apply_backlight(value);
}

uint8_t DeviceM5StacksTab5::getBrightness() {
  return g_brightness;
}

bool DeviceM5StacksTab5::getTouch(int16_t& x, int16_t& y) {
  if (!g_display_ready) {
    return false;
  }

  m5gfx::touch_point_t tp;
  if (!M5.Display.getTouch(&tp, 1)) {
    return false;
  }

  int32_t mapped_x = tp.x;
  int32_t mapped_y = tp.y;
  if (mapped_x < 0) mapped_x = 0;
  if (mapped_y < 0) mapped_y = 0;
  if (mapped_x >= kLogicalWidth) mapped_x = kLogicalWidth - 1;
  if (mapped_y >= kLogicalHeight) mapped_y = kLogicalHeight - 1;

  x = static_cast<int16_t>(mapped_x);
  y = static_cast<int16_t>(mapped_y);
  return true;
}

void DeviceM5StacksTab5::displaySleep() {
  if (!g_display_ready) {
    return;
  }

  M5.Display.waitDMA();
  // Keep the Tab5 panel/touch controller awake. Full display sleep stops
  // touch wake on this device, so sleep mode only blanks the backlight.
  apply_backlight(0);
}

void DeviceM5StacksTab5::displayWake() {
  if (!g_display_ready) {
    return;
  }

  M5.Display.wakeup();
  apply_backlight(g_brightness);
}

void DeviceM5StacksTab5::displayWakeDark() {
  if (!g_display_ready) {
    return;
  }

  M5.Display.wakeup();
  apply_backlight(0);
}

void DeviceM5StacksTab5::displayPowerSaveOn() {
  if (!g_display_ready) {
    return;
  }

  M5.Display.waitDMA();
  // Tab5 touch wake only works while the display controller stays awake.
  // Power save therefore means backlight off, not panel sleep/powerSaveOn.
  apply_backlight(0);
  M5.Display.waitDMA();
}

void DeviceM5StacksTab5::displayPowerSaveOff() {
  if (!g_display_ready) {
    return;
  }

  M5.Display.waitDMA();
  apply_backlight(g_brightness);
}

void DeviceM5StacksTab5::displayWaitDisplay() {
  if (!g_display_ready) {
    return;
  }

  M5.Display.waitDMA();
}

void DeviceM5StacksTab5::prepareForRestart() {
  if (!g_display_ready) {
    return;
  }

  M5.Display.waitDMA();
  M5.Display.fillScreen(0x0000);
  M5.Display.waitDMA();
  apply_backlight(0);
  M5.Display.sleep();
}

bool DeviceM5StacksTab5::initSDCard() {
  if (g_sd_available && SD.cardType() != CARD_NONE) {
    return true;
  }

  const uint32_t now = millis();
  if (g_sd_init_attempted && (now - g_sd_retry_tick_ms) < 1500) {
    return false;
  }
  g_sd_init_attempted = true;
  g_sd_retry_tick_ms = now;

  SD.end();
  SPI.begin(kSdClkPin, kSdMisoPin, kSdMosiPin, kSdCsPin);
  if (!SD.begin(kSdCsPin, SPI, kSdClockHz)) {
    g_sd_available = false;
    Serial.println("[Device/M5StacksTab5] SD card mount failed");
    Serial.flush();
    return false;
  }

  if (SD.cardType() == CARD_NONE) {
    g_sd_available = false;
    Serial.println("[Device/M5StacksTab5] SD card absent after mount");
    Serial.flush();
    SD.end();
    return false;
  }

  g_sd_available = true;
  Serial.printf("[Device/M5StacksTab5] SD card OK, type=%u, size=%llu MB\n",
                static_cast<unsigned>(SD.cardType()),
                static_cast<unsigned long long>(SD.cardSize() / (1024ULL * 1024ULL)));
  Serial.flush();
  return true;
}

bool DeviceM5StacksTab5::storageReady() {
  return g_littlefs_ready;
}

fs::FS& DeviceM5StacksTab5::storageFS() {
  return LittleFS;
}

bool DeviceM5StacksTab5::sdReady() {
  return initSDCard();
}

fs::FS& DeviceM5StacksTab5::sdFS() {
  return SD;
}

bool DeviceM5StacksTab5::suspendSDCardForNetworkTransition() {
  const bool was_mounted = g_sd_available && SD.cardType() != CARD_NONE;
  if (!was_mounted) return false;

  SD.end();
  g_sd_available = false;
  g_sd_init_attempted = false;
  Serial.println("[Device/M5StacksTab5] SD card suspended for network transition");
  return true;
}

bool DeviceM5StacksTab5::resumeSDCardAfterNetworkTransition() {
  g_sd_available = false;
  g_sd_init_attempted = false;
  const bool mounted = initSDCard();
  Serial.printf("[Device/M5StacksTab5] SD card remount after network transition: %s\n",
                mounted ? "OK" : "failed");
  return mounted;
}

bool DeviceM5StacksTab5::initLittleFS() {
  if (g_littlefs_ready) {
    return true;
  }
  if (!LittleFS.begin(true, "/littlefs", 10, "spiffs")) {
    Serial.println("[Device/M5StacksTab5] LittleFS mount failed");
    return false;
  }
  g_littlefs_ready = true;
  Serial.printf("[Device/M5StacksTab5] LittleFS ready, total=%u, used=%u\n",
                static_cast<unsigned>(LittleFS.totalBytes()),
                static_cast<unsigned>(LittleFS.usedBytes()));
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

void DeviceM5StacksTab5::migrateStorageFromSD() {
  if (!g_littlefs_ready) {
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
    copyDirectory(SD, LittleFS, "/_tile_grids");
    copyDirectory(SD, LittleFS, "/_tile_links");
    copyDirectory(SD, LittleFS, "/icons");
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

#endif
