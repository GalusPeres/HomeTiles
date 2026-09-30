#pragma once

#include <stddef.h>
#include <stdint.h>
#include <sdkconfig.h>

#if defined(CONFIG_IDF_TARGET_ESP32P4)

#include <driver/ppa.h>
#include <esp_lcd_panel_ops.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace p4_dsi_camera_presenter {

enum class Transform : uint8_t {
  Native0Or180,
  Portrait90Or270,
};

struct Config {
  int32_t panel_width;
  int32_t panel_height;
  Transform transform;
  uint32_t ppa_timeout_ms;
  uint32_t ppa_grace_ms;
  const char* device_name;
};

struct PpaRuntime {
  ppa_client_handle_t handle;
  SemaphoreHandle_t done;
  bool async_ready;
  bool reset_pending;
  bool cooldown_active;
  void (*note_fault)();
  void (*note_success)();
};

// A submitted PPA operation cannot be cancelled safely in IDF 5.5. Keep the
// caller's detached DMA2D lease held, persist the exact operation and restart
// without touching the panel again.
[[noreturn]] void restartAfterPpaTimeout(
    const char* device_name, const char* operation, int32_t x, int32_t y,
    int32_t w, int32_t h, int32_t source_stride, uint8_t rotation,
    uint32_t waited_ms);

[[noreturn]] void restartAfterDisplayTimeout(
    const char* device_name, const char* operation, int32_t x, int32_t y,
    int32_t w, int32_t h, int32_t source_stride, uint8_t rotation,
    uint32_t waited_ms);

// Shared tear-free presenter for native ESP-IDF DSI panels. Panel controller
// setup, timings, touch and backlight remain entirely device-owned.
class Presenter {
 public:
  bool init(const Config& config, esp_lcd_panel_handle_t panel,
            SemaphoreHandle_t refresh_done, uint16_t* framebuffer0,
            uint16_t* framebuffer1);

  uint16_t* activeFramebuffer() const;
  bool active() const { return double_buffer_active_; }

  // Call after a normal UI write into activeFramebuffer(). PPA writers must
  // set ppa_writer so cached CPU lines cannot later overwrite the DMA result.
  bool noteUiWrite(int32_t x, int32_t y, int32_t w, int32_t h,
                   bool ppa_writer);

  bool present(int32_t x, int32_t y, int32_t w, int32_t h,
               int32_t source_stride, const uint16_t* data,
               size_t data_size, bool byte_swap, uint8_t rotation,
               const PpaRuntime& runtime);

  void end();

  // UI frame swap (opt-in per board): the UI draws into the hidden
  // framebuffer and commitUi() shows the finished frame at the next panel
  // refresh, so a redraw never appears band by band. While the camera path
  // above is active it keeps its own double buffering and UI writes go to
  // the shown framebuffer as before.
  void enableUiFrameSwap();
  // The framebuffer UI writes go to: the hidden one during frame swap,
  // first brought up to date with the shown one; else the shown one.
  uint16_t* uiFramebuffer();
  // Copies a panel rectangle the hidden framebuffer misses from the shown
  // one (the areas LVGL's sync callback reports before it draws).
  void syncUiArea(int32_t x, int32_t y, int32_t w, int32_t h);
  // For a write LVGL does not know about (a full fill): after the next
  // commit the hidden framebuffer is copied from the shown one completely.
  void syncAllAfterCommit();
  // Shows the frame drawn since the last commit at the next panel refresh.
  // The wait for that refresh happens before the next UI write.
  bool commitUi();

 private:
  bool begin();
  bool syncUiToInactive();
  bool faultCooldownActive();
  void noteFault(const PpaRuntime& runtime);
  void noteSuccess(const PpaRuntime& runtime);
  void resetMirrorDirty();
  void markMirrorDirty(int32_t x, int32_t y, int32_t w, int32_t h);
  uint16_t* inactiveFramebuffer() const;
  size_t framebufferBytes() const;
  bool syncCache(const void* data, size_t bytes, bool memory_to_cache) const;
  bool syncFramebufferSpan(uint16_t* framebuffer, int32_t x, int32_t y,
                           int32_t w, int32_t h,
                           bool memory_to_cache) const;
  bool flushFramebufferRect(const uint16_t* framebuffer, int32_t x,
                            int32_t y, int32_t w, int32_t h) const;
  void drainRefreshSignal() const;
  bool waitRefreshDone() const;
  [[noreturn]] void restartAfterTimeout(int32_t x, int32_t y, int32_t w,
                                        int32_t h, int32_t source_stride,
                                        uint8_t rotation) const;
  bool uiSwapActive() const { return ui_frame_swap_ && !double_buffer_active_; }
  bool finishUiSwap();
  bool prepareUiFrame();
  void disableUiFrameSwap(const char* reason);
  void resetUiFrameDirty();
  void markUiFrameDirty(int32_t x, int32_t y, int32_t w, int32_t h);
  void noteUiTiming(uint32_t wait_us, uint32_t sync_us, size_t sync_bytes);

  Config config_{};
  esp_lcd_panel_handle_t panel_ = nullptr;
  SemaphoreHandle_t refresh_done_ = nullptr;
  uint16_t* framebuffers_[2] = {nullptr, nullptr};
  uint8_t active_index_ = 0;
  bool ready_ = false;
  bool double_buffer_active_ = false;
  bool mirror_dirty_ = false;
  int32_t dirty_x1_ = 0;
  int32_t dirty_y1_ = 0;
  int32_t dirty_x2_ = 0;
  int32_t dirty_y2_ = 0;
  uint32_t fault_cooldown_until_ms_ = 0;

  bool ui_frame_swap_ = false;
  bool ui_swap_pending_ = false;
  bool ui_full_sync_ = false;
  bool ui_full_sync_after_commit_ = false;
  bool ui_frame_dirty_ = false;
  int32_t ui_x1_ = 0;
  int32_t ui_y1_ = 0;
  int32_t ui_x2_ = 0;
  int32_t ui_y2_ = 0;
  // Aggregated frame swap timings, logged at most every 30 seconds.
  uint32_t ui_stats_started_ms_ = 0;
  uint32_t ui_frames_ = 0;
  uint32_t ui_wait_total_us_ = 0;
  uint32_t ui_wait_max_us_ = 0;
  uint32_t ui_sync_total_us_ = 0;
  uint32_t ui_sync_max_us_ = 0;
  uint32_t ui_sync_kb_ = 0;
};

}  // namespace p4_dsi_camera_presenter

#endif  // CONFIG_IDF_TARGET_ESP32P4
