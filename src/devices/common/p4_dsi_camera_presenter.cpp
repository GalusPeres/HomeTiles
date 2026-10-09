#include "src/devices/common/p4_dsi_camera_presenter.h"
#include "src/devices/common/p4_dsi_cpu_rotate.h"

#if defined(CONFIG_IDF_TARGET_ESP32P4)

#include <Arduino.h>
#include <algorithm>
#include <cstring>
#include <esp_cache.h>
#include <esp_heap_caps.h>
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
  landscape_transform_ = config.transform;
  if (upright_) config_.transform = Transform::Native0Or180;
  panel_ = panel;
  refresh_done_ = refresh_done;
  framebuffers_[0] = framebuffer0;
  framebuffers_[1] = framebuffer1;
  active_index_ = 0;
  double_buffer_active_ = false;
  refresh_pending_ = false;
  fault_cooldown_until_ms_ = 0;
  resetMirrorDirty();
  ready_ = panel_ && refresh_done_ && framebuffers_[0] && framebuffers_[1] &&
           config_.panel_width > 0 && config_.panel_height > 0;
  return ready_;
}

void Presenter::setUpright(bool upright) {
  upright_ = upright;
  config_.transform = upright ? Transform::Native0Or180 : landscape_transform_;
  Serial.printf("[CameraDisplay/%s] UI %s\n", config_.device_name ? config_.device_name : "P4",
                upright ? "upright (no quarter turn)" : "landscape");
}

bool Presenter::drawUpright(int32_t x, int32_t y, int32_t w, int32_t h,
                            const uint16_t* data, uint8_t rotation) {
  uint16_t* framebuffer = activeFramebuffer();
  if (!framebuffer || !data ||
      !rectInside(x, y, w, h, config_.panel_width, config_.panel_height)) {
    return false;
  }
  const bool flipped = (rotation & 0x02U) != 0;
  const int32_t dst_x = flipped ? config_.panel_width - x - w : x;
  const int32_t dst_y = flipped ? config_.panel_height - y - h : y;
  p4_dsi_cpu_rotate::copy_into(framebuffer, config_.panel_width, config_.panel_width,
                               config_.panel_height, x, y, w, h, data, flipped);
  return flushFramebufferRect(framebuffer, dst_x, dst_y, w, h) &&
         noteUiWrite(dst_x, dst_y, w, h, false);
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

bool Presenter::begin(bool covers_panel) {
  if (double_buffer_active_) return true;
  finishPendingSwap();
  uint16_t* active = activeFramebuffer();
  uint16_t* inactive = inactiveFramebuffer();
  if (!active || !inactive) return false;
  // A partial frame (a camera stream) lands on a copy of the UI. A frame
  // that fills the panel (the screensaver, each opening and slide) replaces
  // every pixel, and the 2 MB copy before it cost the V2 tens of ms (b307).
  if (!covers_panel) {
    if (!syncFramebufferSpan(active, 0, 0, config_.panel_width,
                             config_.panel_height, true)) {
      return false;
    }
    std::memcpy(inactive, active, framebufferBytes());
  }
  // Written back either way: no dirty CPU line of the inactive framebuffer
  // may be evicted over the PPA result later.
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

void Presenter::finishPendingSwap() {
  if (!refresh_pending_) return;
  // The refresh after the swap request is usually long over by the next
  // camera frame, so this rarely waits. It keeps the rule that nothing
  // writes into a framebuffer the panel may still scan.
  if (!waitRefreshDone()) {
    // The driver accepted the swap but did not confirm which framebuffer is
    // now scanned. Writing into the guessed inactive one could tear the live
    // picture, so restart with the pipeline quarantined.
    restartAfterDisplayTimeout(
        config_.device_name, "DSI framebuffer refresh", 0, 0,
        config_.panel_width, config_.panel_height, config_.panel_width, 0,
        kRefreshTimeoutMs);
  }
  refresh_pending_ = false;
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
  // Full-screen frames own both framebuffers until endFullFrames().
  if (!ready_ || full_frames_ || !data || source_stride < w || h <= 0 ||
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

  // The previous frame's swap must be scanned before the now inactive
  // framebuffer is written again.
  finishPendingSwap();
  Dma2dArbiterGuard dma2d_guard(kDma2dLockTimeoutMs);
  if (!dma2d_guard.locked()) return false;
  const bool covers_panel = dst_x == 0 && dst_y == 0 &&
                            dst_w == config_.panel_width &&
                            dst_h == config_.panel_height;
  bool synced = begin(covers_panel);
  if (synced && covers_panel) {
    resetMirrorDirty();  // UI changes since the last frame are overwritten.
  } else if (synced) {
    synced = syncUiToInactive();
  }
  if (!synced) {
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
  // happened before or during the draw call; the next one after this point
  // confirms the new framebuffer. The UI loop does not wait for it here (up
  // to one refresh, about 15 ms at 68.6 Hz): UI writes go to the new
  // framebuffer at once, which the panel shows from that refresh on, and the
  // next present() or end() waits before the old one is written again.
  drainRefreshSignal();
  active_index_ ^= 1U;
  refresh_pending_ = true;
  noteSuccess(runtime);
  return true;
}

bool Presenter::fullFrameInfo(uint8_t rotation, uint16_t& width,
                              uint16_t& height, uint16_t& turn_cw) const {
  if (!ready_) return false;
  width = static_cast<uint16_t>(config_.panel_width);
  height = static_cast<uint16_t>(config_.panel_height);
  // present() turns frames by the PPA's counter-clockwise angle: 90 CCW is
  // 270 clockwise, 270 CCW is 90 clockwise.
  if (config_.transform == Transform::Portrait90Or270) {
    turn_cw = (rotation & 0x02U) ? 270 : 90;
  } else {
    turn_cw = (rotation & 0x02U) ? 180 : 0;
  }
  return true;
}

bool Presenter::swapTo(uint16_t* framebuffer) {
  const esp_err_t swap_err = esp_lcd_panel_draw_bitmap(
      panel_, 0, 0, config_.panel_width, config_.panel_height, framebuffer);
  if (swap_err != ESP_OK) {
    Serial.printf("[CameraDisplay/%s] DSI framebuffer swap failed: %d\n",
                  config_.device_name ? config_.device_name : "P4",
                  static_cast<int>(swap_err));
    return false;
  }
  // As in present(): the next refresh after this point shows the new one.
  drainRefreshSignal();
  active_index_ ^= 1U;
  refresh_pending_ = true;
  return true;
}

// The popup's frame enlarged by the PPA (1/16 steps, as large as the screen
// allows, centred) into `destination`, turned like present() turns it.
bool Presenter::drawPreview(uint16_t* destination, const uint16_t* data,
                            int32_t w, int32_t h, int32_t source_stride,
                            size_t data_size, bool byte_swap, uint8_t rotation,
                            const PpaRuntime& runtime) {
  if (!destination || !data || w <= 0 || h <= 0 || source_stride < w ||
      (reinterpret_cast<uintptr_t>(data) & (kCacheLineSize - 1U)) != 0 ||
      !runtime.handle || !runtime.done || !runtime.async_ready ||
      runtime.reset_pending || runtime.cooldown_active ||
      faultCooldownActive()) {
    return false;
  }
  const size_t required_bytes =
      (static_cast<size_t>(h - 1) * source_stride + w) * sizeof(uint16_t);
  if (data_size < required_bytes) return false;
  const bool quarter = config_.transform == Transform::Portrait90Or270;
  const int32_t logical_w = quarter ? config_.panel_height : config_.panel_width;
  const int32_t logical_h = quarter ? config_.panel_width : config_.panel_height;
  int32_t scale16 = std::min(logical_w * 16 / w, logical_h * 16 / h);
  if (scale16 < 16) return false;
  if (scale16 > 64) scale16 = 64;
  const int32_t out_w = w * scale16 / 16;
  const int32_t out_h = h * scale16 / 16;
  const int32_t x = (logical_w - out_w) / 2;
  const int32_t y = (logical_h - out_h) / 2;
  int32_t dst_x = x;
  int32_t dst_y = y;
  int32_t dst_w = out_w;
  int32_t dst_h = out_h;
  ppa_srm_rotation_angle_t rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
  if (quarter) {
    dst_w = out_h;
    dst_h = out_w;
    if (rotation & 0x02U) {
      dst_x = y;
      dst_y = logical_w - x - out_w;
      rotation_angle = PPA_SRM_ROTATION_ANGLE_90;
    } else {
      dst_x = logical_h - y - out_h;
      dst_y = x;
      rotation_angle = PPA_SRM_ROTATION_ANGLE_270;
    }
  } else if (rotation & 0x02U) {
    dst_x = logical_w - x - out_w;
    dst_y = logical_h - y - out_h;
    rotation_angle = PPA_SRM_ROTATION_ANGLE_180;
  }
  if (!rectInside(dst_x, dst_y, dst_w, dst_h, config_.panel_width,
                  config_.panel_height)) {
    return false;
  }

  Dma2dArbiterGuard dma2d_guard(kDma2dLockTimeoutMs);
  if (!dma2d_guard.locked() || !syncCache(data, required_bytes, false)) return false;
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
  oper.scale_x = static_cast<float>(scale16) / 16.0f;
  oper.scale_y = static_cast<float>(scale16) / 16.0f;
  oper.byte_swap = byte_swap;
  oper.mode = PPA_TRANS_MODE_NON_BLOCKING;
  oper.user_data = runtime.done;
  while (xSemaphoreTake(runtime.done, 0) == pdTRUE) {
  }
  if (ppa_do_scale_rotate_mirror(runtime.handle, &oper) != ESP_OK) return false;
  bool completed =
      xSemaphoreTake(runtime.done, pdMS_TO_TICKS(config_.ppa_timeout_ms)) ==
      pdTRUE;
  if (!completed && config_.ppa_grace_ms > 0) {
    completed =
        xSemaphoreTake(runtime.done, pdMS_TO_TICKS(config_.ppa_grace_ms)) ==
        pdTRUE;
  }
  if (!completed) {
    dma2d_guard.detach();
    restartAfterTimeout(x, y, w, h, source_stride, rotation);
  }
  return syncFramebufferSpan(destination, dst_x, dst_y, dst_w, dst_h, true);
}

bool Presenter::beginFullFrames(const uint16_t* preview, int32_t preview_w,
                                int32_t preview_h, int32_t preview_stride,
                                size_t preview_bytes, bool byte_swap,
                                uint8_t rotation, const PpaRuntime& runtime) {
  if (!ready_) return false;
  if (full_frames_) return true;
  finishPendingSwap();
  uint16_t* active = activeFramebuffer();
  uint16_t* inactive = inactiveFramebuffer();
  const size_t bytes = framebufferBytes();
  if (!active || !inactive) return false;
  if (!ui_copy_) {
    ui_copy_ = static_cast<uint16_t*>(
        heap_caps_aligned_alloc(kCacheLineSize, bytes, MALLOC_CAP_SPIRAM));
  }
  if (!ui_copy_) {
    Serial.printf("[CameraDisplay/%s] Full screen: no memory for the UI copy\n",
                  config_.device_name ? config_.device_name : "P4");
    return false;
  }
  // The UI as the panel scans it, like begin() (PPA writes reach memory only).
  bool ok = syncCache(active, bytes, true);
  if (ok) {
    std::memcpy(ui_copy_, active, bytes);
    // Black at once: the inactive framebuffer cleared, written back, shown;
    // the popup's frame enlarged in its middle until the first full frame.
    std::memset(inactive, 0, bytes);
    ok = syncCache(inactive, bytes, false);
    if (ok && preview) {
      drawPreview(inactive, preview, preview_w, preview_h, preview_stride,
                  preview_bytes, byte_swap, rotation, runtime);
    }
    ok = ok && swapTo(inactive);
  }
  if (!ok) {
    heap_caps_free(ui_copy_);
    ui_copy_ = nullptr;
    return false;
  }
  double_buffer_active_ = true;
  resetMirrorDirty();
  full_frames_ = true;
  Serial.printf("[CameraDisplay/%s] Full screen: UI kept, screen black\n",
                config_.device_name ? config_.device_name : "P4");
  return true;
}

uint16_t* Presenter::acquireFullFrame(size_t& bytes) {
  bytes = 0;
  if (!ready_ || !full_frames_) return nullptr;
  // The decoder may only write a framebuffer the panel no longer scans.
  finishPendingSwap();
  bytes = framebufferBytes();
  return inactiveFramebuffer();
}

bool Presenter::submitFullFrame() {
  if (!ready_ || !full_frames_) return false;
  uint16_t* frame = inactiveFramebuffer();
  // The decoder wrote memory: no stale CPU line may stand for its pixels.
  if (!frame || !syncCache(frame, framebufferBytes(), true)) return false;
  return swapTo(frame);
}

void Presenter::endFullFrames() {
  if (!full_frames_) return;
  full_frames_ = false;
  finishPendingSwap();
  uint16_t* inactive = inactiveFramebuffer();
  if (ui_copy_ && inactive) {
    std::memcpy(inactive, ui_copy_, framebufferBytes());
    if (syncCache(inactive, framebufferBytes(), false)) swapTo(inactive);
  }
  heap_caps_free(ui_copy_);
  ui_copy_ = nullptr;
  // The other framebuffer still holds the last video frame: a later camera
  // frame starts over with a whole copy of the UI (begin()).
  double_buffer_active_ = false;
  resetMirrorDirty();
  Serial.printf("[CameraDisplay/%s] Full screen ended: UI back\n",
                config_.device_name ? config_.device_name : "P4");
}

void Presenter::end() {
  // A stream stopping under the full screen leaves it to endFullFrames().
  if (full_frames_ || !double_buffer_active_) return;
  finishPendingSwap();
  double_buffer_active_ = false;
  resetMirrorDirty();
  Serial.printf("[CameraDisplay/%s] DSI double buffering ended; fb%u remains active\n",
                config_.device_name ? config_.device_name : "P4",
                static_cast<unsigned>(active_index_));
}

}  // namespace p4_dsi_camera_presenter

#endif  // CONFIG_IDF_TARGET_ESP32P4
