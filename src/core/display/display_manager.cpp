#include "src/core/display/display_manager.h"
#include "src/core/power/power_manager.h"
#include "src/core/hardware/board_hal.h"
#include "src/devices/device_select.h"
#include "src/devices/guition_esp32_4848s040/s3_diagnostics.h"
#include "src/core/diagnostics/popup_timing.h"
#if defined(DEVICE_WAVESHARE_TOUCH_LCD_X) || \
    defined(DEVICE_GUITION_JC1060P470C_FAMILY)
#include "src/devices/active_device.h"
#endif
#include "esp_heap_caps.h"
#include "esp_cache.h"
#include <Arduino.h>
#include <cstdint>
#include <cstring>

// Shared instance.
DisplayManager displayManager;

// Static members.
lv_display_t* DisplayManager::disp = nullptr;
lv_indev_t* DisplayManager::indev = nullptr;
lv_color_t* DisplayManager::buf1 = nullptr;
lv_color_t* DisplayManager::buf2 = nullptr;
uint32_t DisplayManager::last_activity_time = 0;
uint8_t DisplayManager::rotation = 0;
static bool g_ignore_touch_until_release = false;
static bool g_input_enabled = true;
static volatile uint16_t g_flush_log_budget = 0;
static size_t g_buffer_lines = 0;
static uint8_t g_bytes_per_pixel = 0;
static lv_display_render_mode_t g_render_mode = LV_DISPLAY_RENDER_MODE_PARTIAL;
static bool g_reverse_flush = false;
static lv_color_t* g_reverse_buf = nullptr;
static size_t g_reverse_buf_width = 0;
static constexpr size_t kReverseStripeWidth = 16;
static constexpr bool kEnableReverseFlushEffect = true;
static constexpr uintptr_t kCacheLineSize = 64;
static bool g_reverse_flush_once = false;
static volatile uint32_t g_fullscreen_flush_seq = 0;
static size_t g_requested_buffer_lines = 0;
static bool g_fast_internal_draw_buffer = false;
static bool g_single_psram_draw_buffer = false;
static lv_color_t* g_preserved_buf1 = nullptr;
static lv_color_t* g_preserved_buf2 = nullptr;
static size_t g_preserved_buffer_lines = 0;
static size_t g_preserved_requested_buffer_lines = 0;
static lv_display_render_mode_t g_preserved_render_mode =
    LV_DISPLAY_RENDER_MODE_PARTIAL;
static bool g_preserved_fast_internal_draw_buffer = false;
static bool g_preserved_single_psram_draw_buffer = false;
static bool g_preserved_draw_buffer_active = false;

#if HOMETILES_GUITION_S3_DIAGNOSTICS_ACTIVE
static void guition_s3_indev_event_cb(lv_event_t* event) {
  const lv_event_code_t code = lv_event_get_code(event);
  GuitionS3Diagnostics::TouchAction action;
  switch (code) {
    case LV_EVENT_SHORT_CLICKED:
      action = GuitionS3Diagnostics::TouchAction::ShortClicked;
      break;
    case LV_EVENT_CLICKED:
      action = GuitionS3Diagnostics::TouchAction::Clicked;
      break;
    case LV_EVENT_LONG_PRESSED:
      action = GuitionS3Diagnostics::TouchAction::LongPressed;
      break;
    default:
      return;
  }

  lv_point_t point{};
  lv_indev_t* event_indev =
      static_cast<lv_indev_t*>(lv_event_get_current_target(event));
  if (event_indev) lv_indev_get_point(event_indev, &point);
  GuitionS3Diagnostics::noteTouchAction(
      action, millis(), lv_event_get_param(event),
      static_cast<int16_t>(point.x), static_cast<int16_t>(point.y));
}
#endif

#if defined(DEVICE_M5STACKS_TAB5)
struct Tab5FlushStats {
  uint32_t frame_start_us = 0;
  uint32_t flushes = 0;
  uint32_t dma_flushes = 0;
  uint32_t cpu_flushes = 0;
  uint32_t pixels = 0;
  uint32_t total_us = 0;
  uint32_t push_us = 0;
  uint32_t wait_us = 0;
  uint32_t cache_us = 0;
  uint32_t max_flush_us = 0;
  uint32_t frame_no = 0;
};

static Tab5FlushStats g_tab5_flush_stats;
static uint8_t g_tab5_flush_log_budget = 12;

static void tab5_note_flush(uint32_t pixels, uint32_t flush_us, uint32_t push_us,
                            uint32_t wait_us, uint32_t cache_us, bool used_dma,
                            bool used_cpu, bool is_last) {
  g_tab5_flush_stats.flushes++;
  if (used_dma) g_tab5_flush_stats.dma_flushes++;
  if (used_cpu) g_tab5_flush_stats.cpu_flushes++;
  g_tab5_flush_stats.pixels += pixels;
  g_tab5_flush_stats.total_us += flush_us;
  g_tab5_flush_stats.push_us += push_us;
  g_tab5_flush_stats.wait_us += wait_us;
  g_tab5_flush_stats.cache_us += cache_us;
  if (flush_us > g_tab5_flush_stats.max_flush_us) {
    g_tab5_flush_stats.max_flush_us = flush_us;
  }

  if (!is_last) {
    return;
  }

  if (g_tab5_flush_log_budget > 0) {
    const uint32_t wall_us = micros() - g_tab5_flush_stats.frame_start_us;
    Serial.printf("[Tab5/Flush] frame=%lu flushes=%lu dma=%lu cpu=%lu px=%lu wall=%lu ms flush=%lu ms push=%lu ms wait=%lu ms cache=%lu ms max=%lu us\n",
                  static_cast<unsigned long>(g_tab5_flush_stats.frame_no),
                  static_cast<unsigned long>(g_tab5_flush_stats.flushes),
                  static_cast<unsigned long>(g_tab5_flush_stats.dma_flushes),
                  static_cast<unsigned long>(g_tab5_flush_stats.cpu_flushes),
                  static_cast<unsigned long>(g_tab5_flush_stats.pixels),
                  static_cast<unsigned long>(wall_us / 1000),
                  static_cast<unsigned long>(g_tab5_flush_stats.total_us / 1000),
                  static_cast<unsigned long>(g_tab5_flush_stats.push_us / 1000),
                  static_cast<unsigned long>(g_tab5_flush_stats.wait_us / 1000),
                  static_cast<unsigned long>(g_tab5_flush_stats.cache_us / 1000),
                  static_cast<unsigned long>(g_tab5_flush_stats.max_flush_us));
    g_tab5_flush_log_budget--;
  }

  g_tab5_flush_stats.frame_no++;
  g_tab5_flush_stats.frame_start_us = 0;
  g_tab5_flush_stats.flushes = 0;
  g_tab5_flush_stats.dma_flushes = 0;
  g_tab5_flush_stats.cpu_flushes = 0;
  g_tab5_flush_stats.pixels = 0;
  g_tab5_flush_stats.total_us = 0;
  g_tab5_flush_stats.push_us = 0;
  g_tab5_flush_stats.wait_us = 0;
  g_tab5_flush_stats.cache_us = 0;
  g_tab5_flush_stats.max_flush_us = 0;
}
#endif

// --- Draw buffer placement --------------------------------------------------
// The 8-inch panel renders 1280x800 = 1.0 Mpx. Software-rasterising every frame
// into PSRAM is the dominant cost on this board (PSRAM write bandwidth). We try
// to place a small LVGL draw band into fast internal SRAM instead. A single
// buffer is enough here: the flush is fully synchronous (blocking PPA rotate),
// so a second buffer would bring no render/flush overlap anyway. If internal RAM
// is too scarce we fall back to the previous PSRAM double buffer, so behaviour
// is never worse than before.
static constexpr size_t kInternalDrawReserveBytes = 150 * 1024;  // keep free for WiFi/SDIO/lwIP
static constexpr size_t kInternalDrawMaxBytes     = 72 * 1024;   // cap one SRAM band
static constexpr size_t kInternalDrawMinLines     = 16;          // below this SRAM isn't worth it
// Restore after OTA preparation: Wi-Fi, MQTT and the server already own
// their runtime RAM. Reusing the large boot reserve would count it twice
// and reject the SRAM band. The Tab5 log from 2026-07-06 showed slow PSRAM
// rendering until reboot after each failed update. About 80 KB of headroom
// matches the measured stable point: roughly 100 KB DMA free with the band active.
static constexpr size_t kInternalDrawRestoreReserveBytes = 80 * 1024;
// Camera rendering may temporarily use one full-height LVGL band. Keep a
// generous PSRAM floor for decoded frames, folder caches and later features.
// This check is independent from the internal/DMA heap used by WiFi and MQTT.
static constexpr size_t kLargeDrawPsramReserveBytes = 8 * 1024 * 1024;

static inline void commit_display_if_last(lv_display_t* lv_disp) {
#if defined(DEVICE_WAVESHARE_TOUCH_LCD_X) || \
    defined(DEVICE_GUITION_JC1060P470C_FAMILY)
  if (lv_display_flush_is_last(lv_disp)) {
    DeviceImpl::displayCommit();
  }
#elif defined(DEVICE_ESP32_S3_RGB_480)
  if (lv_display_flush_is_last(lv_disp)) {
    DeviceImpl::displayWaitDisplay();
  }
#else
  (void)lv_disp;
#endif
}

static inline void flush_cache_for_dma(const void* ptr, size_t size) {
#if defined(DEVICE_ESP32_S3_RGB_480)
  // This board's "DMA" entry point is a synchronous CPU copy into
  // Arduino_GFX's live framebuffer. The LVGL source buffer is never consumed
  // by GDMA, so synchronising it is both unnecessary and wrong. In Arduino
  // core 3.3.7 the old 64-byte rounding can extend outside a 32-byte-cache-line
  // PSRAM allocation and esp_cache_msync() rejects the range; the device log
  // then shows one error for nearly every larger UI flush.
  (void)ptr;
  (void)size;
  return;
#endif
  if (!ptr || size == 0) return;
  const uintptr_t start = reinterpret_cast<uintptr_t>(ptr);
  const uintptr_t aligned_start = start & ~(kCacheLineSize - 1);
  const uintptr_t end = start + size;
  const uintptr_t aligned_end = (end + kCacheLineSize - 1) & ~(kCacheLineSize - 1);
  if (aligned_end <= aligned_start) return;
  esp_cache_msync(reinterpret_cast<void*>(aligned_start),
                  aligned_end - aligned_start,
                  ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_TYPE_DATA);
}

static bool ensure_reverse_buf() {
  if (g_reverse_buf && g_reverse_buf_width == kReverseStripeWidth) return true;
  if (g_reverse_buf) {
    heap_caps_free(g_reverse_buf);
    g_reverse_buf = nullptr;
    g_reverse_buf_width = 0;
  }
  uint8_t bpp = g_bytes_per_pixel ? g_bytes_per_pixel : 2;
  const size_t bytes = kReverseStripeWidth * SCREEN_HEIGHT * bpp;
  lv_color_t* buf = (lv_color_t*)heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  if (!buf) {
    buf = (lv_color_t*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
  }
  if (!buf) return false;
  g_reverse_buf = buf;
  g_reverse_buf_width = kReverseStripeWidth;
  return true;
}

void DisplayManager::debugFlushNext(uint16_t count) {
  g_flush_log_budget = count;
}

void DisplayManager::setReverseFlush(bool enable) {
  if (!kEnableReverseFlushEffect) {
    if (g_reverse_buf) {
      heap_caps_free(g_reverse_buf);
      g_reverse_buf = nullptr;
    }
    g_reverse_buf_width = 0;
    g_reverse_flush = false;
    g_reverse_flush_once = false;
    return;
  }
  if (enable == g_reverse_flush) return;
  if (!enable) {
    if (g_reverse_buf) {
      heap_caps_free(g_reverse_buf);
      g_reverse_buf = nullptr;
    }
    g_reverse_buf_width = 0;
    g_reverse_flush = false;
    g_reverse_flush_once = false;
    return;
  }
  if (!ensure_reverse_buf()) {
    g_reverse_flush = false;
    g_reverse_flush_once = false;
    return;
  }
  g_reverse_flush = true;
  g_reverse_flush_once = false;
}

void DisplayManager::setReverseFlushOnce() {
  if (!kEnableReverseFlushEffect) return;
  if (!ensure_reverse_buf()) return;
  g_reverse_flush = true;
  g_reverse_flush_once = true;
}

bool DisplayManager::allocDrawBuffers(size_t requested_lines, lv_display_render_mode_t mode) {
  return allocDrawBuffers(requested_lines, mode, kInternalDrawReserveBytes, false);
}

bool DisplayManager::allocDrawBuffers(size_t requested_lines, lv_display_render_mode_t mode,
                                      size_t internal_reserve_bytes,
                                      bool require_fast_internal) {
  if (!disp || requested_lines == 0) return false;
  if (g_preserved_draw_buffer_active) {
    Serial.println(
        "[Display] Buffer switch rejected: camera buffer is still active");
    return false;
  }
  if (g_bytes_per_pixel == 0) {
    g_bytes_per_pixel = lv_color_format_get_size(lv_display_get_color_format(disp));
    if (g_bytes_per_pixel == 0) g_bytes_per_pixel = 2;
  }
  const size_t line_bytes = (size_t)SCREEN_WIDTH * g_bytes_per_pixel;
  if (line_bytes == 0) return false;

  lv_color_t* nb1 = nullptr;
  lv_color_t* nb2 = nullptr;
  size_t use_lines = requested_lines;
  bool single = false;
  bool psram = false;

  // 1) Preferred: one small band in fast internal SRAM.
  const size_t free_internal =
      heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  const size_t budget =
      (free_internal > internal_reserve_bytes) ? (free_internal - internal_reserve_bytes) : 0;
  size_t cap_bytes = budget < kInternalDrawMaxBytes ? budget : kInternalDrawMaxBytes;
  // Account for fragmentation: after longer uptime the largest contiguous
  // DMA block can be much smaller than total free memory (Tab5: 171 KB free,
  // largest block 69 KB). Prefer fewer SRAM rows to a full PSRAM fallback.
  const size_t largest_block =
      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  const size_t largest_usable = (largest_block > 2048) ? (largest_block - 2048) : 0;
  if (cap_bytes > largest_usable) cap_bytes = largest_usable;
  size_t sram_lines = cap_bytes / line_bytes;
  if (sram_lines > requested_lines) sram_lines = requested_lines;
  if (sram_lines >= kInternalDrawMinLines) {
    nb1 = (lv_color_t*)heap_caps_aligned_alloc(64, line_bytes * sram_lines,
                                               MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (nb1) {
      use_lines = sram_lines;
      single = true;
    }
  }

  // After failed OTA, a failed SRAM restoration must not silently count
  // as a successful PSRAM restoration. Retain the small OTA buffer and
  // retry once HTTP/TCP buffers are released and DMA heap can coalesce.
  if (!nb1 && require_fast_internal) {
    Serial.printf(
        "[Display] Fast SRAM restore not yet possible "
        "(dma free=%u KB, largest=%u KB)\n",
        (unsigned)(free_internal / 1024),
        (unsigned)(largest_block / 1024));
    return false;
  }

  // 2) Fallback: PSRAM double buffer at the requested size (previous behaviour).
  if (!nb1) {
    const size_t bytes = line_bytes * requested_lines;
    nb1 = (lv_color_t*)heap_caps_aligned_alloc(64, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    nb2 = (lv_color_t*)heap_caps_aligned_alloc(64, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    if (nb1 && nb2) {
      psram = true;
    } else {
      if (nb1) heap_caps_free(nb1);
      if (nb2) heap_caps_free(nb2);
      // 3) Last resort: internal double buffer.
      nb1 = (lv_color_t*)heap_caps_aligned_alloc(64, bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
      nb2 = (lv_color_t*)heap_caps_aligned_alloc(64, bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
      if (!nb1 || !nb2) {
        if (nb1) heap_caps_free(nb1);
        if (nb2) heap_caps_free(nb2);
        return false;
      }
    }
    use_lines = requested_lines;
    single = false;
  }

  const size_t buf_bytes = line_bytes * use_lines;
  lv_display_set_buffers(disp, nb1, nb2, buf_bytes, mode);

  if (buf1) heap_caps_free(buf1);
  if (buf2) heap_caps_free(buf2);
  buf1 = nb1;
  buf2 = nb2;
  g_buffer_lines = use_lines;
  g_requested_buffer_lines = requested_lines;
  g_render_mode = mode;
  g_fast_internal_draw_buffer = single;
  g_single_psram_draw_buffer = false;

  Serial.printf("[Display] Draw buffer: %s %s, %u lines, %u bytes/buffer | int free=%u KB | dma free=%u KB | dma largest=%u KB\n",
                single ? "1x" : "2x",
                single ? "SRAM(fast)" : (psram ? "PSRAM" : "SRAM"),
                (unsigned)use_lines, (unsigned)buf_bytes,
                (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA) / 1024),
                (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA) / 1024));
  return true;
}

bool DisplayManager::setBufferLines(size_t lines) {
  return setBufferLines(lines, LV_DISPLAY_RENDER_MODE_PARTIAL);
}

bool DisplayManager::setSinglePsramBufferLines(size_t lines) {
  if (!disp || lines == 0 || lines > SCREEN_HEIGHT) return false;
  if (g_preserved_draw_buffer_active) {
    if (g_single_psram_draw_buffer && g_buffer_lines == lines && buf1 &&
        !buf2) {
      return true;
    }
    Serial.println(
        "[Display] Temporary PSRAM draw buffer already active");
    return false;
  }
  if (g_single_psram_draw_buffer && g_buffer_lines == lines && buf1 && !buf2) {
    return true;
  }
  if (g_bytes_per_pixel == 0) {
    g_bytes_per_pixel =
        lv_color_format_get_size(lv_display_get_color_format(disp));
    if (g_bytes_per_pixel == 0) g_bytes_per_pixel = 2;
  }

  const size_t bytes =
      static_cast<size_t>(SCREEN_WIDTH) * lines * g_bytes_per_pixel;
  // On ESP32-P4 the PPA can read cache-synchronised external RAM, but the
  // generic heap does not advertise PSRAM as MALLOC_CAP_DMA. Requiring both
  // capabilities therefore reports zero available bytes and silently kept the
  // camera on the small band buffer. Allocate ordinary byte-addressable PSRAM;
  // the device flush path performs the required cache write-back before PPA.
  const uint32_t psram_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
  const size_t free_psram = heap_caps_get_free_size(psram_caps);
  const size_t largest_psram =
      heap_caps_get_largest_free_block(psram_caps);
  if (free_psram < bytes + kLargeDrawPsramReserveBytes ||
      largest_psram < bytes) {
    Serial.printf(
        "[Display] Large PSRAM draw buffer rejected: required=%u KB "
        "free=%u KB largest=%u KB reserve=%u KB\n",
        static_cast<unsigned>(bytes / 1024U),
        static_cast<unsigned>(free_psram / 1024U),
        static_cast<unsigned>(largest_psram / 1024U),
        static_cast<unsigned>(kLargeDrawPsramReserveBytes / 1024U));
    return false;
  }

  lv_color_t* next = static_cast<lv_color_t*>(
      heap_caps_aligned_alloc(64, bytes, psram_caps));
  if (!next) {
    Serial.println("[Display] Unable to allocate large PSRAM draw buffer");
    return false;
  }

  // Finish all rendering that still references the old draw buffer before
  // swapping it out. A single buffer is sufficient because every P4 flush is
  // synchronous and calls lv_display_flush_ready() only after PPA completion.
  lv_refr_now(disp);

  // Keep the normal UI draw buffer allocated while the camera is open. Freeing
  // and reallocating the fast SRAM band fragmented the internal DMA heap after
  // repeated camera sessions. Eventually only the slower PSRAM fallback could
  // be restored, making every later folder switch about 80 ms slower.
  g_preserved_buf1 = buf1;
  g_preserved_buf2 = buf2;
  g_preserved_buffer_lines = g_buffer_lines;
  g_preserved_requested_buffer_lines = g_requested_buffer_lines;
  g_preserved_render_mode = g_render_mode;
  g_preserved_fast_internal_draw_buffer = g_fast_internal_draw_buffer;
  g_preserved_single_psram_draw_buffer = g_single_psram_draw_buffer;
  g_preserved_draw_buffer_active = true;

  lv_display_set_buffers(disp, next, nullptr, bytes,
                         LV_DISPLAY_RENDER_MODE_PARTIAL);

  buf1 = next;
  buf2 = nullptr;
  g_buffer_lines = lines;
  g_requested_buffer_lines = lines;
  g_render_mode = LV_DISPLAY_RENDER_MODE_PARTIAL;
  g_fast_internal_draw_buffer = false;
  g_single_psram_draw_buffer = true;

  Serial.printf(
      "[Display] Camera draw buffer: 1x PSRAM, %u lines, %u KB | "
      "PSRAM free=%u KB\n",
      static_cast<unsigned>(lines),
      static_cast<unsigned>(bytes / 1024U),
      static_cast<unsigned>(
          heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024U));
  return true;
}

bool DisplayManager::restoreDrawBufferAfterSinglePsram() {
  if (!disp || !g_preserved_draw_buffer_active || !g_preserved_buf1 ||
      g_preserved_buffer_lines == 0 || g_bytes_per_pixel == 0) {
    return false;
  }

  // Complete rendering through the temporary camera buffer before LVGL is
  // pointed back at the retained UI buffer.
  lv_refr_now(disp);

  lv_color_t* temporary_buf1 = buf1;
  lv_color_t* temporary_buf2 = buf2;
  const size_t restored_bytes =
      static_cast<size_t>(SCREEN_WIDTH) * g_preserved_buffer_lines *
      g_bytes_per_pixel;
  lv_display_set_buffers(disp, g_preserved_buf1, g_preserved_buf2,
                         restored_bytes, g_preserved_render_mode);

  buf1 = g_preserved_buf1;
  buf2 = g_preserved_buf2;
  g_buffer_lines = g_preserved_buffer_lines;
  g_requested_buffer_lines = g_preserved_requested_buffer_lines;
  g_render_mode = g_preserved_render_mode;
  g_fast_internal_draw_buffer = g_preserved_fast_internal_draw_buffer;
  g_single_psram_draw_buffer = g_preserved_single_psram_draw_buffer;

  g_preserved_buf1 = nullptr;
  g_preserved_buf2 = nullptr;
  g_preserved_buffer_lines = 0;
  g_preserved_requested_buffer_lines = 0;
  g_preserved_render_mode = LV_DISPLAY_RENDER_MODE_PARTIAL;
  g_preserved_fast_internal_draw_buffer = false;
  g_preserved_single_psram_draw_buffer = false;
  g_preserved_draw_buffer_active = false;

  if (temporary_buf1 && temporary_buf1 != buf1) {
    heap_caps_free(temporary_buf1);
  }
  if (temporary_buf2 && temporary_buf2 != buf2) {
    heap_caps_free(temporary_buf2);
  }

  Serial.printf(
      "[Display] Reserved UI draw buffer active again: %s, "
      "%u lines, %u bytes\n",
      g_fast_internal_draw_buffer ? "SRAM(fast)" : "PSRAM",
      static_cast<unsigned>(g_buffer_lines),
      static_cast<unsigned>(restored_bytes));
  return true;
}

bool DisplayManager::restoreBufferLinesAfterOta(size_t lines) {
  if (!disp || lines == 0) return false;
  lv_refr_now(disp);
  // Always allocate again, without setBufferLines()'s equality shortcut:
  // the current buffer is the small OTA PSRAM allocation; the target is
  // the fast SRAM band with the runtime reserve.
  return allocDrawBuffers(lines, LV_DISPLAY_RENDER_MODE_PARTIAL,
                          kInternalDrawRestoreReserveBytes, true);
}

bool DisplayManager::isUsingFastInternalBuffer() const {
  return g_fast_internal_draw_buffer;
}

bool DisplayManager::setBufferLines(size_t lines, lv_display_render_mode_t render_mode) {
  if (!disp || lines == 0) {
    return false;
  }
  if (g_bytes_per_pixel == 0) {
    g_bytes_per_pixel = lv_color_format_get_size(lv_display_get_color_format(disp));
    if (g_bytes_per_pixel == 0) {
      g_bytes_per_pixel = 2;
    }
  }
  if (g_requested_buffer_lines == lines && g_render_mode == render_mode) {
    return true;
  }

  lv_refr_now(disp);

  if (g_requested_buffer_lines == lines && g_render_mode != render_mode && buf1) {
    const size_t bytes = (size_t)SCREEN_WIDTH * g_buffer_lines * g_bytes_per_pixel;
    lv_display_set_buffers(disp, buf1, buf2, bytes, render_mode);
    g_render_mode = render_mode;
    Serial.printf("[Display] Render mode changed: %d (lines=%d)\n", (int)render_mode, (int)g_buffer_lines);
    return true;
  }

  return allocDrawBuffers(lines, render_mode);
}

size_t DisplayManager::getBufferLines() const {
  return g_buffer_lines;
}

size_t DisplayManager::getRequestedBufferLines() const {
  return g_requested_buffer_lines;
}

lv_display_render_mode_t DisplayManager::getRenderMode() const {
  return g_render_mode;
}

uint32_t DisplayManager::getFullScreenFlushSeq() const {
  return g_fullscreen_flush_seq;
}

void DisplayManager::setRotation(uint8_t rotation_value) {
  rotation_value = Device::normalizeRotationQuarterTurns(rotation_value);
  if (rotation == rotation_value) return;
  BoardHAL::displaySetRotation(rotation_value);
  rotation = rotation_value;
  lv_display_t* disp_local = lv_display_get_default();
  if (disp_local) {
    lv_obj_invalidate(lv_scr_act());
    lv_refr_now(disp_local);
  }
}

void DisplayManager::setRotationFlipped(bool flipped) {
  setRotation(flipped ? Device::kRotationFlipped : Device::kRotationDefault);
}

bool DisplayManager::isRotationFlipped() const {
  return rotation == Device::kRotationFlipped;
}

uint8_t DisplayManager::getRotation() const {
  return rotation;
}

// ========== Display Flush Callback ==========
// IRAM_ATTR keeps this frequently called per-frame callback in internal
// RAM, avoiding instruction-cache misses during display updates.
void IRAM_ATTR DisplayManager::flush_cb(lv_display_t *lv_disp, const lv_area_t *area, uint8_t *px_map) {
#if HOMETILES_GUITION_S3_DIAGNOSTICS_ACTIVE
  const uint32_t s3_flush_started_us = micros();
#endif
  const uint32_t w = (area->x2 - area->x1 + 1);
  const uint32_t h = (area->y2 - area->y1 + 1);
  const size_t row_bytes = (size_t)w * sizeof(uint16_t);
  const size_t stride_bytes = lv_draw_buf_width_to_stride(w, lv_display_get_color_format(lv_disp));
  const bool packed_rows = (stride_bytes == row_bytes);
  const uint32_t area_px = w * h;
  const bool is_last_flush = lv_display_flush_is_last(lv_disp);
  static constexpr uint32_t kMinPixelsForDma = 2048;  // avoid DMA overhead on tiny dirty areas
  static constexpr uint32_t kReverseMinPixels = (SCREEN_WIDTH * SCREEN_HEIGHT) / 8;  // trigger only on large image updates
#if defined(DEVICE_M5STACKS_TAB5)
  const uint32_t tab5_flush_start_us = micros();
  if (g_tab5_flush_stats.flushes == 0 && g_tab5_flush_stats.frame_start_us == 0) {
    g_tab5_flush_stats.frame_start_us = tab5_flush_start_us;
  }
  uint32_t tab5_push_us = 0;
  uint32_t tab5_wait_us = 0;
  uint32_t tab5_cache_us = 0;
  bool tab5_used_dma = false;
  bool tab5_used_cpu = false;
#endif
  if (g_flush_log_budget) {
    Serial.printf("[FLUSH] x=%d..%d y=%d..%d w=%lu h=%lu last=%d\n",
                  area->x1, area->x2, area->y1, area->y2,
                  (unsigned long)w, (unsigned long)h,
                  is_last_flush);
    g_flush_log_budget--;
  }
  if (g_reverse_flush && g_reverse_buf && g_reverse_buf_width > 0 &&
      area_px >= kReverseMinPixels) {
    const uint16_t* src = reinterpret_cast<const uint16_t*>(px_map);
    const uint32_t stripe = (uint32_t)g_reverse_buf_width;
    uint32_t x = 0;
    while (x < w) {
      const uint32_t remaining = w - x;
      const uint32_t cur_w = (remaining >= stripe) ? stripe : remaining;
      const uint32_t start = x;
      uint16_t* dst = reinterpret_cast<uint16_t*>(g_reverse_buf);
      for (uint32_t row = 0; row < h; ++row) {
        const uint16_t* src_row = reinterpret_cast<const uint16_t*>(
            reinterpret_cast<const uint8_t*>(src) + row * stride_bytes) + start;
        std::memcpy(dst + row * cur_w, src_row, cur_w * sizeof(uint16_t));
      }
#if defined(DEVICE_M5STACKS_TAB5)
      const uint32_t push_start_us = micros();
#endif
      BoardHAL::displayPushPixelsDMA(area->x1 + (int32_t)start, area->y1, cur_w, h, dst);
#if defined(DEVICE_M5STACKS_TAB5)
      tab5_push_us += micros() - push_start_us;
      tab5_used_dma = true;
#endif
      x += cur_w;
    }
    if (g_reverse_flush_once) {
      g_reverse_flush = false;
      g_reverse_flush_once = false;
    }
    if (area->x1 == 0 && area->y1 == 0 && w == SCREEN_WIDTH && h == SCREEN_HEIGHT) {
      g_fullscreen_flush_seq++;
    }
    commit_display_if_last(lv_disp);
#if defined(DEVICE_M5STACKS_TAB5)
    tab5_note_flush(area_px, micros() - tab5_flush_start_us, tab5_push_us,
                    tab5_wait_us, tab5_cache_us, tab5_used_dma, tab5_used_cpu,
                    is_last_flush);
#endif
#if HOMETILES_GUITION_S3_DIAGNOSTICS_ACTIVE
    GuitionS3Diagnostics::noteFlush(
        area_px, micros() - s3_flush_started_us, is_last_flush,
        static_cast<int16_t>(area->x1), static_cast<int16_t>(area->y1),
        static_cast<int16_t>(area->x2), static_cast<int16_t>(area->y2));
#endif
    lv_display_flush_ready(lv_disp);
    return;
  }

  // Hybrid flush path:
  // - small/partial areas via CPU pushImage (robust for odd widths and tiny regions)
  // - larger areas via DMA when rows are tightly packed
  const bool use_dma = packed_rows && (area_px >= kMinPixelsForDma);
  if (use_dma) {
#if defined(DEVICE_M5STACKS_TAB5)
    uint32_t step_start_us = micros();
#endif
    flush_cache_for_dma(px_map, row_bytes * h);
#if defined(DEVICE_M5STACKS_TAB5)
    tab5_cache_us += micros() - step_start_us;
    step_start_us = micros();
#endif
    BoardHAL::displayPushPixelsDMA(area->x1, area->y1, w, h, (uint16_t*)px_map);
#if defined(DEVICE_M5STACKS_TAB5)
    tab5_push_us += micros() - step_start_us;
    step_start_us = micros();
#endif
    BoardHAL::displayWaitDMA();
#if defined(DEVICE_M5STACKS_TAB5)
    tab5_wait_us += micros() - step_start_us;
    tab5_used_dma = true;
#endif
  } else {
    if (packed_rows) {
#if defined(DEVICE_M5STACKS_TAB5)
      const uint32_t step_start_us = micros();
#endif
      BoardHAL::displayPushPixels(area->x1, area->y1, w, h, (uint16_t*)px_map);
#if defined(DEVICE_M5STACKS_TAB5)
      tab5_push_us += micros() - step_start_us;
      tab5_used_cpu = true;
#endif
    } else {
      // LVGL can align each line in px_map; push line-by-line when rows are padded.
      for (uint32_t row = 0; row < h; ++row) {
        const uint16_t* src_row = reinterpret_cast<const uint16_t*>(
            reinterpret_cast<const uint8_t*>(px_map) + row * stride_bytes);
#if defined(DEVICE_M5STACKS_TAB5)
        const uint32_t step_start_us = micros();
#endif
        BoardHAL::displayPushPixels(area->x1, area->y1 + (int32_t)row, w, 1, (uint16_t*)src_row);
#if defined(DEVICE_M5STACKS_TAB5)
        tab5_push_us += micros() - step_start_us;
        tab5_used_cpu = true;
#endif
      }
    }
  }

  if (area->x1 == 0 && area->y1 == 0 && w == SCREEN_WIDTH && h == SCREEN_HEIGHT) {
    g_fullscreen_flush_seq++;
  }
  commit_display_if_last(lv_disp);
#if defined(DEVICE_M5STACKS_TAB5)
  tab5_note_flush(area_px, micros() - tab5_flush_start_us, tab5_push_us,
                  tab5_wait_us, tab5_cache_us, tab5_used_dma, tab5_used_cpu,
                  is_last_flush);
#endif
#if HOMETILES_GUITION_S3_DIAGNOSTICS_ACTIVE
  GuitionS3Diagnostics::noteFlush(
      area_px, micros() - s3_flush_started_us, is_last_flush,
      static_cast<int16_t>(area->x1), static_cast<int16_t>(area->y1),
      static_cast<int16_t>(area->x2), static_cast<int16_t>(area->y2));
#endif
  lv_display_flush_ready(lv_disp);
}

// ========== Touch Callback ==========
// IRAM_ATTR keeps frequent touch polling in internal RAM for responsive input.
void IRAM_ATTR DisplayManager::touch_cb(lv_indev_t* indev_drv, lv_indev_data_t *data) {
  // Wake from display sleep only on real touch. The normal loop does not
  // drive this callback during sleep, but lvglServiceDuringBlockingWork()
  // can call lv_timer_handler(), for example while logList() prints a large
  // Bridge configuration. Without the touch check, each Bridge update woke
  // the sleeping display without any physical input.
  if (powerManager.isInSleep()) {
    if (powerManager.isTouchWakeEnabled()) {
      BoardHAL::TouchPoint tmp;
      if (BoardHAL::getTouch(&tmp)) {
        powerManager.wakeFromDisplaySleep("touch_cb");
        g_ignore_touch_until_release = true;  // Wait for release before accepting input again.
      }
    }
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }

  // Block input while sleeping or explicitly disabled.
  if (!g_input_enabled) {
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }

  // After sleep, require touch release before accepting actions.
  if (g_ignore_touch_until_release) {
    BoardHAL::TouchPoint tmp;
    if (BoardHAL::getTouch(&tmp)) {
      data->state = LV_INDEV_STATE_RELEASED;
      return;
    }
    g_ignore_touch_until_release = false;
  }

  BoardHAL::TouchPoint tp;
  if (BoardHAL::getTouch(&tp)) {
    int16_t mapped_x = tp.x;
    int16_t mapped_y = tp.y;
#if !defined(DEVICE_M5STACKS_TAB5) && \
    !defined(DEVICE_WAVESHARE_TOUCH_LCD_X) && \
    !defined(DEVICE_GUITION_JC8012P4A1_FAMILY) && \
    !defined(DEVICE_GUITION_JC1060P470C_FAMILY) && \
    !defined(DEVICE_ESP32_S3_RGB_480)
    switch (rotation & 0x03) {
      case 1:
        mapped_x = tp.y;
        mapped_y = SCREEN_WIDTH - 1 - tp.x;
        break;
      case 2:
        mapped_x = SCREEN_WIDTH - 1 - tp.x;
        mapped_y = SCREEN_HEIGHT - 1 - tp.y;
        break;
      case 3:
        mapped_x = SCREEN_HEIGHT - 1 - tp.y;
        mapped_y = tp.x;
        break;
      default:
        break;
    }
#endif

    data->state = LV_INDEV_STATE_PRESSED;
    data->point.x = mapped_x;
    data->point.y = mapped_y;

    // Reset the activity timer and wake the power manager.
    last_activity_time = millis();
    powerManager.setHighPerformance(true);
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

// ========== Initialization ==========
bool DisplayManager::init() {
  Serial.println("[Display] Initializing Display Manager...");

  // BoardHAL::init() already initialized the active device's display.
  // Native geometry and rotation come from its profile.
  BoardHAL::displayFillScreen(0x0000);  // black
#if !defined(DEVICE_M5STACKS_TAB5) && \
    !defined(DEVICE_WAVESHARE_TOUCH_LCD_X) && \
    !defined(DEVICE_GUITION_JC8012P4A1_FAMILY) && \
    !defined(DEVICE_GUITION_JC1060P470C_FAMILY) && \
    !defined(DEVICE_ESP32_S3_RGB_480)
  BoardHAL::setBrightness(150);  // The power manager controls this later.
#endif
  rotation = Device::kRotationDefault;

  last_activity_time = millis();

  // LVGL's built-in TLSF allocator assumes that LV_MEM_SIZE can be obtained
  // as one contiguous block. Passing a null pool into lv_init() causes a
  // StoreProhibited panic before LVGL can return an error. Check the exact
  // requirement while no other task can fragment PSRAM and fail with a useful
  // serial message instead.
  const size_t largest_lvgl_pool =
      heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (largest_lvgl_pool < LV_MEM_SIZE) {
    Serial.printf(
        "[Display] LVGL pool unavailable: required=%u KB, "
        "largest PSRAM block=%u KB\n",
        static_cast<unsigned>(LV_MEM_SIZE / 1024U),
        static_cast<unsigned>(largest_lvgl_pool / 1024U));
    return false;
  }

  // Initialize LVGL.
  lv_init();

  // Create the display.
  disp = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);
  if (!disp) {
    Serial.println("[Display] Display creation failed!");
    return false;
  }

  lv_display_set_flush_cb(disp, flush_cb);

#if defined(DEVICE_M5STACKS_TAB5)
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565_SWAPPED);
#else
  lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
#endif
  // Enable anti-aliasing for curved edges on switches, popups and radio
  // buttons. Solid fills need no extra work; fonts already use 4 bpp.
  lv_display_set_antialiasing(disp, true);

  // Initialize the default theme with the HomeTiles turquoise accent;
  // otherwise LVGL uses blue for focus rings, switches and checkboxes.
  // Keep dark=false (LV_THEME_DEFAULT_DARK 0); only the accent changes.
  if (lv_theme_t* theme = lv_theme_default_init(disp, lv_color_hex(0x26A69A),
                                                lv_color_hex(0xC14444), false,
                                                LV_FONT_DEFAULT)) {
    lv_display_set_theme(disp, theme);
  }
  g_bytes_per_pixel = lv_color_format_get_size(lv_display_get_color_format(disp));
  if (g_bytes_per_pixel == 0) {
    g_bytes_per_pixel = 2;
  }

  // Smaller DMA buffers leave more heap available for tile-heavy grids.
  static constexpr size_t TARGET_LINES = SCREEN_HEIGHT / Device::kDisplayFlushBands;
  if (!allocDrawBuffers(TARGET_LINES, LV_DISPLAY_RENDER_MODE_PARTIAL)) {
    Serial.println("[Display] DMA buffer allocation failed!");
    return false;
  }
#if HOMETILES_GUITION_S3_DIAGNOSTICS_ACTIVE
  Serial.printf(
      "[S3Diag/Display] phase=lvgl-init result=ok resolution=%dx%d "
      "lv_color=RGB565 byte_swap=0 bytes_per_pixel=%u render=partial "
      "buffer_lines=%u requested_lines=%u buffers=%s buf1=%p buf2=%p\n",
      SCREEN_WIDTH, SCREEN_HEIGHT, static_cast<unsigned>(g_bytes_per_pixel),
      static_cast<unsigned>(g_buffer_lines),
      static_cast<unsigned>(g_requested_buffer_lines),
      g_fast_internal_draw_buffer
          ? "internal-fast"
          : (g_single_psram_draw_buffer ? "psram-single" : "psram-double"),
      static_cast<void*>(buf1), static_cast<void*>(buf2));
#endif

  // Touch-Input
  indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touch_cb);
  lv_indev_set_display(indev, disp);
#if defined(HOMETILES_POPUP_TIMING)
  popup_timing::attach(disp, indev);
#endif
#if HOMETILES_GUITION_S3_DIAGNOSTICS_ACTIVE
  lv_indev_add_event_cb(indev, guition_s3_indev_event_cb, LV_EVENT_ALL,
                        nullptr);
#endif
#if defined(DEVICE_WAVESHARE_TOUCH_LCD_X) || \
    defined(DEVICE_GUITION_JC8012P4A1_FAMILY) || \
    defined(DEVICE_GUITION_JC1060P470C_FAMILY) || \
    defined(DEVICE_ESP32_S3_RGB_480)
  if (lv_timer_t* read_timer = lv_indev_get_read_timer(indev)) {
    lv_timer_set_period(read_timer, 8);
    Serial.println("[Display] Large-panel touch poll period set to 8 ms");
  }
#endif

  Serial.println("[OK] Display Manager initialized");
  return true;
}

void DisplayManager::resetActivityTimer() {
  last_activity_time = millis();
}

void DisplayManager::armWakeTouchGuard() {
  g_ignore_touch_until_release = true;
}

void DisplayManager::setInputEnabled(bool enable) {
  g_input_enabled = enable;
  if (indev) {
    lv_indev_enable(indev, enable);
    if (!enable) {
      lv_indev_reset(indev, nullptr);
    }
  }
}
