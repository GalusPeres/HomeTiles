#include "p4_dsi_ui_ppa.h"

#if defined(CONFIG_IDF_TARGET_ESP32P4)

#include <Arduino.h>
#include <esp_cache.h>
#include "src/core/display/dma2d_arbiter.h"

namespace p4_dsi_ui_ppa {
namespace {

constexpr uint32_t kPpaRotateTimeoutMs = 200;
constexpr uint32_t kPpaPreviewGraceMs = 800;
constexpr uint32_t kPpaFaultCooldownMs = 1200;
constexpr uint32_t kPpaReinitRetryMs = 1000;
constexpr uint8_t kPpaFaultsBeforeReset = 2;
constexpr uintptr_t kCacheLineSize = 64;
// Preserve the Waveshare 8-inch/Tab5 guards: narrow scrolling titles and
// strips shorter than eight rows can wedge the P4 SRM completion queue.
constexpr int32_t kPpaMinRotateWidth = 600;
constexpr int32_t kPpaMinRotateHeight = 8;

bool IRAM_ATTR on_ppa_trans_done(ppa_client_handle_t, ppa_event_data_t*,
                                void* user_data) {
  auto sem = static_cast<SemaphoreHandle_t>(user_data);
  if (!sem) return false;
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR(sem, &woken);
  return woken == pdTRUE;
}

}  // namespace

bool Client::cooldownActive() const {
  return cooldown_until_ms_ != 0 &&
         static_cast<int32_t>(millis() - cooldown_until_ms_) < 0;
}

void Client::pauseFor(uint32_t duration_ms) {
  if (!duration_ms) return;
  const uint32_t until = millis() + duration_ms;
  if (!cooldown_until_ms_ ||
      static_cast<int32_t>(until - cooldown_until_ms_) > 0) {
    cooldown_until_ms_ = until;
  }
}

void Client::scheduleReinit() {
  reset_pending_ = true;
  reinit_at_ms_ = millis() + kPpaReinitRetryMs;
  if (!reinit_at_ms_) reinit_at_ms_ = 1;
}

void Client::init() {
  reset(true);
}

void Client::reset(bool initial) {
  async_ready_ = false;
  reset_pending_ = true;
  if (handle_) {
    const esp_err_t err = ppa_unregister_client(handle_);
    if (err != ESP_OK) {
      // An outstanding transaction still owns the handle and its buffers.
      // Retain it until retirement succeeds; never leak its pending slot.
      scheduleReinit();
      Serial.printf("[Device/%s] PPA client busy err=%d, retry reset in %lu ms\n",
                    device_name_, static_cast<int>(err),
                    static_cast<unsigned long>(kPpaReinitRetryMs));
      return;
    }
    handle_ = nullptr;
  }

  ppa_client_config_t config = {};
  config.oper_type = PPA_OPERATION_SRM;
  const esp_err_t err = ppa_register_client(&config, &handle_);
  if (err != ESP_OK || !handle_) {
    handle_ = nullptr;
    scheduleReinit();
    Serial.printf("[Device/%s] PPA client register failed err=%d, retry in %lu ms\n",
                  device_name_, static_cast<int>(err),
                  static_cast<unsigned long>(kPpaReinitRetryMs));
    return;
  }
  if (initial) Serial.printf("[Device/%s] PPA client registered\n", device_name_);
  if (!done_) done_ = xSemaphoreCreateBinary();
  if (done_) {
    ppa_event_callbacks_t callbacks = {};
    callbacks.on_trans_done = on_ppa_trans_done;
    async_ready_ =
        ppa_client_register_event_callbacks(handle_, &callbacks) == ESP_OK;
  }
  if (!async_ready_) {
    scheduleReinit();
    Serial.printf("[Device/%s] PPA timeout callback unavailable, retrying client reset\n",
                  device_name_);
    return;
  }
  reinit_at_ms_ = 0;
  reset_pending_ = false;
  Serial.printf("[Device/%s] %s\n", device_name_, initial
                    ? "PPA timeout-safe mode ready"
                    : "PPA client reset after fault");
}

void Client::noteFault() {
  if (++consecutive_faults_ >= kPpaFaultsBeforeReset) {
    reset();
    consecutive_faults_ = 0;
  }
  pauseFor(kPpaFaultCooldownMs);
}

p4_dsi_camera_presenter::PpaRuntime Client::runtime(void (*note_fault)()) const {
  return {handle_, done_, async_ready_, reset_pending_, cooldownActive(),
          note_fault, nullptr};
}

Result Client::rotate(p4_dsi_camera_presenter::Presenter& presenter,
                      int32_t panel_width, int32_t panel_height,
                      int32_t x, int32_t y, int32_t w, int32_t h,
                      const uint16_t* data, uint8_t rotation) {
  if (reinit_at_ms_ && static_cast<int32_t>(millis() - reinit_at_ms_) >= 0) {
    reinit_at_ms_ = 0;
    reset();
  }
  if (!data || w <= 0 || h <= 0 || x < 0 || y < 0 ||
      w > panel_height || h > panel_width ||
      x > panel_height - w || y > panel_width - h) {
    return Result::Failed;
  }
  if (!(handle_ && async_ready_ && !reset_pending_ && !cooldownActive() &&
        w >= kPpaMinRotateWidth && h >= kPpaMinRotateHeight)) {
    return Result::CpuFallback;
  }
  uint16_t* framebuffer = presenter.activeFramebuffer();
  if (!framebuffer) return Result::CpuFallback;

  const uintptr_t start = reinterpret_cast<uintptr_t>(data);
  const uintptr_t aligned_start = start & ~(kCacheLineSize - 1);
  const size_t source_bytes = static_cast<size_t>(w) * h * sizeof(uint16_t);
  const uintptr_t aligned_end =
      (start + source_bytes + kCacheLineSize - 1) & ~(kCacheLineSize - 1);
  if (esp_cache_msync(reinterpret_cast<void*>(aligned_start),
                      aligned_end - aligned_start,
                      ESP_CACHE_MSYNC_FLAG_DIR_C2M |
                          ESP_CACHE_MSYNC_FLAG_TYPE_DATA) != ESP_OK) {
    noteFault();
    return Result::CpuFallback;
  }

  const int32_t dst_x = (rotation & 0x02) ? y : panel_width - y - h;
  const int32_t dst_y = (rotation & 0x02) ? panel_height - x - w : x;
  ppa_srm_oper_config_t oper = {};
  oper.in.buffer = data;
  oper.in.pic_w = w;
  oper.in.pic_h = h;
  oper.in.block_w = w;
  oper.in.block_h = h;
  oper.in.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
  oper.out.buffer = framebuffer;
  oper.out.buffer_size =
      static_cast<size_t>(panel_width) * panel_height * sizeof(uint16_t);
  oper.out.pic_w = panel_width;
  oper.out.pic_h = panel_height;
  oper.out.block_offset_x = dst_x;
  oper.out.block_offset_y = dst_y;
  oper.out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
  oper.rotation_angle = (rotation & 0x02) ? PPA_SRM_ROTATION_ANGLE_90
                                         : PPA_SRM_ROTATION_ANGLE_270;
  oper.scale_x = 1.0f;
  oper.scale_y = 1.0f;
  oper.mode = PPA_TRANS_MODE_NON_BLOCKING;
  oper.user_data = done_;

  Dma2dArbiterGuard dma2d_guard(25);
  if (!done_ || !dma2d_guard.locked()) return Result::CpuFallback;
  xSemaphoreTake(done_, 0);
  const esp_err_t err = ppa_do_scale_rotate_mirror(handle_, &oper);
  if (err != ESP_OK) {
    Serial.printf("[Device/%s] PPA rotate submit failed err=%d -> CPU cooldown\n",
                  device_name_, static_cast<int>(err));
    noteFault();
    return Result::CpuFallback;
  }
  if (xSemaphoreTake(done_, pdMS_TO_TICKS(kPpaRotateTimeoutMs)) != pdTRUE &&
      xSemaphoreTake(done_, pdMS_TO_TICKS(kPpaPreviewGraceMs)) != pdTRUE) {
    // Accepted DMA work cannot be cancelled. Keep ownership through restart;
    // a CPU fallback here would race an engine still writing the framebuffer.
    dma2d_guard.detach();
    p4_dsi_camera_presenter::restartAfterPpaTimeout(
        device_name_, "LVGL PPA rotation", x, y, w, h, w, rotation,
        kPpaRotateTimeoutMs + kPpaPreviewGraceMs);
  }
  noteSuccess();
  if (!presenter.noteUiWrite(dst_x, dst_y, h, w, true)) {
    Serial.printf("[Device/%s] UI framebuffer cache sync failed\n", device_name_);
    return Result::Failed;
  }
  return Result::Drawn;
}

}  // namespace p4_dsi_ui_ppa

#endif
