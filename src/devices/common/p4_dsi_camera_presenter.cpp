#include "src/devices/common/p4_dsi_camera_presenter.h"

#if defined(CONFIG_IDF_TARGET_ESP32P4)

#include <Arduino.h>
#include <cstring>
#include <esp_cache.h>
#include <freertos/task.h>

#include "src/core/hardware/board_hal.h"
#include "src/core/diagnostics/crash_log.h"
#include "src/core/display/dma2d_arbiter.h"

namespace p4_dsi_camera_presenter {
namespace {

constexpr uintptr_t kCacheLineSize = 64;
constexpr uint32_t kDma2dLockTimeoutMs = 25;
constexpr uint32_t kRefreshTimeoutMs = 50;
constexpr uint32_t kFaultCooldownMs = 1200;

bool rectInside(int32_t x, int32_t y, int32_t w, int32_t h,
                int32_t bounds_w, int32_t bounds_h) {
  return x >= 0 && y >= 0 && w > 0 && h > 0 && x <= bounds_w - w &&
         y <= bounds_h - h;
}

}  // namespace

[[noreturn]] void restartAfterPpaTimeout(
    const char* device_name, const char* operation, int32_t x, int32_t y,
    int32_t w, int32_t h, int32_t source_stride, uint8_t rotation,
    uint32_t waited_ms) {
  restartAfterDisplayTimeout(device_name, operation, x, y, w, h,
                             source_stride, rotation, waited_ms);
}

[[noreturn]] void restartAfterDisplayTimeout(
    const char* device_name, const char* operation, int32_t x, int32_t y,
    int32_t w, int32_t h, int32_t source_stride, uint8_t rotation,
    uint32_t waited_ms) {
  const char* safe_device = device_name ? device_name : "P4 DSI";
  const char* safe_operation = operation ? operation : "PPA operation";
  String detail;
  detail.reserve(192);
  detail += "Device: ";
  detail += safe_device;
  detail += "\nOperation: ";
  detail += safe_operation;
  detail += "\nGeometry: ";
  detail += String(x);
  detail += ",";
  detail += String(y);
  detail += " ";
  detail += String(w);
  detail += "x";
  detail += String(h);
  detail += " stride=";
  detail += String(source_stride);
  detail += " rotation=";
  detail += String(rotation & 0x03U);
  detail += " waited_ms=";
  detail += String(waited_ms);
  detail += "\nDisplay pipeline resources were quarantined before restart.";
  CrashLog::appendDisplayPipelineTimeoutReport(detail);
  Serial.printf(
      "[P4PPA/%s] %s completion missing after %lu ms; restarting with "
      "buffers quarantined\n",
      safe_device, safe_operation, static_cast<unsigned long>(waited_ms));
  BoardHAL::restart();
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

bool Presenter::init(const Config& config, esp_lcd_panel_handle_t panel,
                     SemaphoreHandle_t refresh_done,
                     uint16_t* framebuffer0, uint16_t* framebuffer1) {
  config_ = config;
  panel_ = panel;
  refresh_done_ = refresh_done;
  framebuffers_[0] = framebuffer0;
  framebuffers_[1] = framebuffer1;
  active_index_ = 0;
  double_buffer_active_ = false;
  fault_cooldown_until_ms_ = 0;
  resetMirrorDirty();
  ready_ = panel_ && refresh_done_ && framebuffers_[0] && framebuffers_[1] &&
           config_.panel_width > 0 && config_.panel_height > 0;
  return ready_;
}

size_t Presenter::framebufferBytes() const {
  return static_cast<size_t>(config_.panel_width) *
         static_cast<size_t>(config_.panel_height) * sizeof(uint16_t);
}

uint16_t* Presenter::activeFramebuffer() const {
  return ready_ ? framebuffers_[active_index_] : nullptr;
}

uint16_t* Presenter::inactiveFramebuffer() const {
  return ready_ ? framebuffers_[active_index_ ^ 1U] : nullptr;
}

bool Presenter::syncCache(const void* data, size_t bytes,
                          bool memory_to_cache) const {
  if (!data || bytes == 0) return false;
  const uintptr_t start = reinterpret_cast<uintptr_t>(data);
  const uintptr_t aligned_start = start & ~(kCacheLineSize - 1U);
  const uintptr_t end = start + bytes;
  const uintptr_t aligned_end =
      (end + kCacheLineSize - 1U) & ~(kCacheLineSize - 1U);
  if (aligned_end <= aligned_start) return false;
  uint32_t flags = ESP_CACHE_MSYNC_FLAG_TYPE_DATA;
  if (memory_to_cache) {
    flags |= ESP_CACHE_MSYNC_FLAG_DIR_M2C |
             ESP_CACHE_MSYNC_FLAG_INVALIDATE;
  } else {
    flags |= ESP_CACHE_MSYNC_FLAG_DIR_C2M;
  }
  return esp_cache_msync(reinterpret_cast<void*>(aligned_start),
                         aligned_end - aligned_start, flags) == ESP_OK;
}

bool Presenter::syncFramebufferSpan(uint16_t* framebuffer, int32_t x,
                                    int32_t y, int32_t w, int32_t h,
                                    bool memory_to_cache) const {
  if (!framebuffer ||
      !rectInside(x, y, w, h, config_.panel_width, config_.panel_height)) {
    return false;
  }
  uint16_t* first =
      framebuffer + static_cast<size_t>(y) * config_.panel_width + x;
  const size_t span_pixels =
      static_cast<size_t>(h - 1) * config_.panel_width + w;
  return syncCache(first, span_pixels * sizeof(uint16_t), memory_to_cache);
}

bool Presenter::flushFramebufferRect(const uint16_t* framebuffer, int32_t x,
                                     int32_t y, int32_t w, int32_t h) const {
  if (!framebuffer ||
      !rectInside(x, y, w, h, config_.panel_width, config_.panel_height)) {
    return false;
  }
  const size_t row_bytes = static_cast<size_t>(w) * sizeof(uint16_t);
  for (int32_t row = 0; row < h; ++row) {
    if (!syncCache(framebuffer +
                       static_cast<size_t>(y + row) * config_.panel_width + x,
                   row_bytes, false)) {
      return false;
    }
  }
  return true;
}

void Presenter::resetMirrorDirty() {
  mirror_dirty_ = false;
  dirty_x1_ = 0;
  dirty_y1_ = 0;
  dirty_x2_ = 0;
  dirty_y2_ = 0;
}

void Presenter::markMirrorDirty(int32_t x, int32_t y, int32_t w, int32_t h) {
  if (!double_buffer_active_ || w <= 0 || h <= 0) return;
  if (!mirror_dirty_) {
    dirty_x1_ = x;
    dirty_y1_ = y;
    dirty_x2_ = x + w - 1;
    dirty_y2_ = y + h - 1;
    mirror_dirty_ = true;
    return;
  }
  if (x < dirty_x1_) dirty_x1_ = x;
  if (y < dirty_y1_) dirty_y1_ = y;
  const int32_t x2 = x + w - 1;
  const int32_t y2 = y + h - 1;
  if (x2 > dirty_x2_) dirty_x2_ = x2;
  if (y2 > dirty_y2_) dirty_y2_ = y2;
}

bool Presenter::noteUiWrite(int32_t x, int32_t y, int32_t w, int32_t h,
                            bool ppa_writer) {
  uint16_t* active = activeFramebuffer();
  if (!active ||
      !rectInside(x, y, w, h, config_.panel_width, config_.panel_height)) {
    return false;
  }
  if (ppa_writer && !syncFramebufferSpan(active, x, y, w, h, true)) {
    return false;
  }
  markMirrorDirty(x, y, w, h);
  return true;
}

bool Presenter::begin() {
  if (double_buffer_active_) return true;
  uint16_t* active = activeFramebuffer();
  uint16_t* inactive = inactiveFramebuffer();
  if (!active || !inactive ||
      !syncFramebufferSpan(active, 0, 0, config_.panel_width,
                           config_.panel_height, true)) {
    return false;
  }
  std::memcpy(inactive, active, framebufferBytes());
  if (!syncCache(inactive, framebufferBytes(), false)) return false;
  resetMirrorDirty();
  double_buffer_active_ = true;
  Serial.printf("[CameraDisplay/%s] DSI double buffering enabled: fb%u -> fb%u\n",
                config_.device_name ? config_.device_name : "P4",
                static_cast<unsigned>(active_index_),
                static_cast<unsigned>(active_index_ ^ 1U));
  return true;
}

bool Presenter::syncUiToInactive() {
  if (!double_buffer_active_ || !mirror_dirty_) return true;
  uint16_t* active = activeFramebuffer();
  uint16_t* inactive = inactiveFramebuffer();
  if (!active || !inactive) return false;

  const int32_t x = dirty_x1_;
  const int32_t y = dirty_y1_;
  const int32_t w = dirty_x2_ - dirty_x1_ + 1;
  const int32_t h = dirty_y2_ - dirty_y1_ + 1;
  const size_t row_bytes = static_cast<size_t>(w) * sizeof(uint16_t);
  for (int32_t row = 0; row < h; ++row) {
    const size_t offset =
        static_cast<size_t>(y + row) * config_.panel_width + x;
    std::memcpy(inactive + offset, active + offset, row_bytes);
  }
  if (!flushFramebufferRect(inactive, x, y, w, h)) return false;
  resetMirrorDirty();
  return true;
}

bool Presenter::faultCooldownActive() {
  if (!fault_cooldown_until_ms_) return false;
  if (static_cast<int32_t>(millis() - fault_cooldown_until_ms_) < 0) {
    return true;
  }
  fault_cooldown_until_ms_ = 0;
  return false;
}

void Presenter::noteFault(const PpaRuntime& runtime) {
  fault_cooldown_until_ms_ = millis() + kFaultCooldownMs;
  if (!fault_cooldown_until_ms_) fault_cooldown_until_ms_ = 1;
  if (runtime.note_fault) runtime.note_fault();
}

void Presenter::noteSuccess(const PpaRuntime& runtime) {
  fault_cooldown_until_ms_ = 0;
  if (runtime.note_success) runtime.note_success();
}

void Presenter::drainRefreshSignal() const {
  if (!refresh_done_) return;
  while (xSemaphoreTake(refresh_done_, 0) == pdTRUE) {
  }
}

bool Presenter::waitRefreshDone() const {
  if (!refresh_done_) return false;
  if (xSemaphoreTake(refresh_done_, pdMS_TO_TICKS(kRefreshTimeoutMs)) ==
      pdTRUE) {
    return true;
  }
  Serial.printf("[CameraDisplay/%s] DSI refresh callback timeout\n",
                config_.device_name ? config_.device_name : "P4");
  return false;
}

[[noreturn]] void Presenter::restartAfterTimeout(
    int32_t x, int32_t y, int32_t w, int32_t h, int32_t source_stride,
    uint8_t rotation) const {
  p4_dsi_camera_presenter::restartAfterPpaTimeout(
      config_.device_name, "camera/full-frame PPA presentation", x, y, w, h,
      source_stride, rotation,
      config_.ppa_timeout_ms + config_.ppa_grace_ms);
}

bool Presenter::present(int32_t x, int32_t y, int32_t w, int32_t h,
                        int32_t source_stride, const uint16_t* data,
                        size_t data_size, bool byte_swap, uint8_t rotation,
                        const PpaRuntime& runtime) {
  if (!ready_ || !data || source_stride < w || h <= 0 ||
      (reinterpret_cast<uintptr_t>(data) & (kCacheLineSize - 1U)) != 0 ||
      !runtime.handle || !runtime.done || !runtime.async_ready ||
      runtime.reset_pending || runtime.cooldown_active ||
      faultCooldownActive()) {
    return false;
  }

  const size_t required_bytes =
      (static_cast<size_t>(h - 1) * source_stride + w) * sizeof(uint16_t);
  if (data_size < required_bytes) return false;

  const int32_t logical_w =
      config_.transform == Transform::Portrait90Or270
          ? config_.panel_height
          : config_.panel_width;
  const int32_t logical_h =
      config_.transform == Transform::Portrait90Or270
          ? config_.panel_width
          : config_.panel_height;
  if (!rectInside(x, y, w, h, logical_w, logical_h)) return false;

  int32_t dst_x = x;
  int32_t dst_y = y;
  int32_t dst_w = w;
  int32_t dst_h = h;
  ppa_srm_rotation_angle_t rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
  if (config_.transform == Transform::Portrait90Or270) {
    dst_w = h;
    dst_h = w;
    if (rotation & 0x02U) {
      dst_x = y;
      dst_y = logical_w - x - w;
      rotation_angle = PPA_SRM_ROTATION_ANGLE_90;
    } else {
      dst_x = logical_h - y - h;
      dst_y = x;
      rotation_angle = PPA_SRM_ROTATION_ANGLE_270;
    }
  } else if (rotation & 0x02U) {
    dst_x = logical_w - x - w;
    dst_y = logical_h - y - h;
    rotation_angle = PPA_SRM_ROTATION_ANGLE_180;
  }
  if (!rectInside(dst_x, dst_y, dst_w, dst_h, config_.panel_width,
                  config_.panel_height)) {
    return false;
  }

  Dma2dArbiterGuard dma2d_guard(kDma2dLockTimeoutMs);
  if (!dma2d_guard.locked()) return false;
  if (!begin() || !syncUiToInactive()) {
    Serial.printf("[CameraDisplay/%s] Framebuffer synchronization failed\n",
                  config_.device_name ? config_.device_name : "P4");
    noteFault(runtime);
    return false;
  }

  uint16_t* destination = inactiveFramebuffer();
  if (!destination || !syncCache(data, required_bytes, false)) {
    Serial.printf("[CameraDisplay/%s] PPA source cache sync failed\n",
                  config_.device_name ? config_.device_name : "P4");
    noteFault(runtime);
    return false;
  }

  ppa_srm_oper_config_t oper = {};
  oper.in.buffer = data;
  oper.in.pic_w = source_stride;
  oper.in.pic_h = h;
  oper.in.block_w = w;
  oper.in.block_h = h;
  oper.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
  oper.out.buffer = destination;
  oper.out.buffer_size = framebufferBytes();
  oper.out.pic_w = config_.panel_width;
  oper.out.pic_h = config_.panel_height;
  oper.out.block_offset_x = dst_x;
  oper.out.block_offset_y = dst_y;
  oper.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
  oper.rotation_angle = rotation_angle;
  oper.scale_x = 1.0f;
  oper.scale_y = 1.0f;
  oper.rgb_swap = false;
  oper.byte_swap = byte_swap;
  oper.mode = PPA_TRANS_MODE_NON_BLOCKING;
  oper.user_data = runtime.done;

  while (xSemaphoreTake(runtime.done, 0) == pdTRUE) {
  }
  const esp_err_t submit_err =
      ppa_do_scale_rotate_mirror(runtime.handle, &oper);
  if (submit_err != ESP_OK) {
    Serial.printf("[CameraDisplay/%s] PPA submit failed: %d\n",
                  config_.device_name ? config_.device_name : "P4",
                  static_cast<int>(submit_err));
    noteFault(runtime);
    return false;
  }

  bool completed =
      xSemaphoreTake(runtime.done, pdMS_TO_TICKS(config_.ppa_timeout_ms)) ==
      pdTRUE;
  if (!completed && config_.ppa_grace_ms > 0) {
    completed =
        xSemaphoreTake(runtime.done, pdMS_TO_TICKS(config_.ppa_grace_ms)) ==
        pdTRUE;
  }
  if (!completed) {
    // The IDF has no force-end operation for a wedged PPA SRM transaction.
    // Returning would release the DMA2D lease and let the caller recycle both
    // source and destination while hardware may still own them. Keep every
    // resource quarantined and make the reset explicit and diagnosable.
    dma2d_guard.detach();
    restartAfterTimeout(x, y, w, h, source_stride, rotation);
  }

  if (!syncFramebufferSpan(destination, dst_x, dst_y, dst_w, dst_h, true)) {
    Serial.printf("[CameraDisplay/%s] PPA output cache sync failed\n",
                  config_.device_name ? config_.device_name : "P4");
    noteFault(runtime);
    return false;
  }

  const esp_err_t swap_err = esp_lcd_panel_draw_bitmap(
      panel_, dst_x, dst_y, dst_x + dst_w, dst_y + dst_h, destination);
  if (swap_err != ESP_OK) {
    Serial.printf("[CameraDisplay/%s] DSI framebuffer swap failed: %d\n",
                  config_.device_name ? config_.device_name : "P4",
                  static_cast<int>(swap_err));
    noteFault(runtime);
    return false;
  }
  // The IDF refresh callback is a continuous VSYNC/frame-end signal, not a
  // one-shot completion owned by draw_bitmap(). Discard every boundary that
  // happened before or during the draw call, then wait for the next one after
  // the driver accepted the new framebuffer selection.
  drainRefreshSignal();
  if (!waitRefreshDone()) {
    // The driver accepted the swap but did not confirm which framebuffer is
    // now scanned. Continuing with a guessed active index can make UI and PPA
    // write into the live scanout buffer concurrently.
    dma2d_guard.detach();
    restartAfterDisplayTimeout(
        config_.device_name, "DSI framebuffer refresh", dst_x, dst_y, dst_w,
        dst_h, config_.panel_width, rotation, kRefreshTimeoutMs);
  }
  active_index_ ^= 1U;
  noteSuccess(runtime);
  return true;
}

void Presenter::end() {
  if (!double_buffer_active_) return;
  double_buffer_active_ = false;
  resetMirrorDirty();
  Serial.printf("[CameraDisplay/%s] DSI double buffering ended; fb%u remains active\n",
                config_.device_name ? config_.device_name : "P4",
                static_cast<unsigned>(active_index_));
}

}  // namespace p4_dsi_camera_presenter

#endif  // CONFIG_IDF_TARGET_ESP32P4
