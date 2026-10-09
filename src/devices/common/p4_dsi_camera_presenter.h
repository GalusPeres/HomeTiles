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
  // The upright layout (grid_layout::turned()): the UI and the camera frames
  // go into the panel without the quarter turn the landscape UI needs.
  void setUpright(bool upright);
  bool upright() const { return upright_; }
  // An upright UI area straight into the active framebuffer (rotation 2:
  // turned by 180 into the mirrored place), written back for the scan-out.
  bool drawUpright(int32_t x, int32_t y, int32_t w, int32_t h,
                   const uint16_t* data, uint8_t rotation);

  // Call after a normal UI write into activeFramebuffer(). PPA writers must
  // set ppa_writer so cached CPU lines cannot later overwrite the DMA result.
  bool noteUiWrite(int32_t x, int32_t y, int32_t w, int32_t h,
                   bool ppa_writer);

  bool present(int32_t x, int32_t y, int32_t w, int32_t h,
               int32_t source_stride, const uint16_t* data,
               size_t data_size, bool byte_swap, uint8_t rotation,
               const PpaRuntime& runtime);

  // Camera full screen (#65): frames that already have the panel's size and
  // orientation go straight from the JPEG decoder into the inactive
  // framebuffer and are swapped in, without the PPA. fullFrameInfo gives that
  // size and the clockwise turn the Bridge applies (the PPA's turn for this
  // rotation). beginFullFrames keeps a copy of the UI shown now and turns the
  // screen black at once, with `preview` (the popup's frame, may be null)
  // enlarged by the PPA in its middle until the first full frame;
  // endFullFrames puts the copy back at once. LVGL must not draw in between.
  bool fullFrameInfo(uint8_t rotation, uint16_t& width, uint16_t& height,
                     uint16_t& turn_cw) const;
  bool beginFullFrames(const uint16_t* preview, int32_t preview_w,
                       int32_t preview_h, int32_t preview_stride,
                       size_t preview_bytes, bool byte_swap, uint8_t rotation,
                       const PpaRuntime& runtime);
  // The framebuffer the decoder may write (nullptr: none), its size in bytes.
  uint16_t* acquireFullFrame(size_t& bytes);
  bool submitFullFrame();
  void endFullFrames();
  bool fullFramesActive() const { return full_frames_; }

  void end();

 private:
  // covers_panel: the frame about to be presented fills the whole panel, so
  // the inactive framebuffer needs no copy of the active one first.
  bool begin(bool covers_panel);
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
  void finishPendingSwap();
  [[noreturn]] void restartAfterTimeout(int32_t x, int32_t y, int32_t w,
                                        int32_t h, int32_t source_stride,
                                        uint8_t rotation) const;

  Config config_{};
  // The driver's transform for the landscape UI (init()); upright the
  // frames go in Native0Or180.
  Transform landscape_transform_ = Transform::Portrait90Or270;
  bool upright_ = false;
  esp_lcd_panel_handle_t panel_ = nullptr;
  SemaphoreHandle_t refresh_done_ = nullptr;
  uint16_t* framebuffers_[2] = {nullptr, nullptr};
  uint8_t active_index_ = 0;
  bool ready_ = false;
  bool double_buffer_active_ = false;
  // A swap was requested but the panel may still scan the previous (now
  // inactive) framebuffer until its next refresh. Nothing writes into the
  // inactive framebuffer before finishPendingSwap() confirmed that refresh.
  bool refresh_pending_ = false;
  bool mirror_dirty_ = false;
  int32_t dirty_x1_ = 0;
  int32_t dirty_y1_ = 0;
  int32_t dirty_x2_ = 0;
  int32_t dirty_y2_ = 0;
  uint32_t fault_cooldown_until_ms_ = 0;
  bool swapTo(uint16_t* framebuffer);
  bool drawPreview(uint16_t* destination, const uint16_t* data, int32_t w,
                   int32_t h, int32_t source_stride, size_t data_size,
                   bool byte_swap, uint8_t rotation, const PpaRuntime& runtime);
  bool full_frames_ = false;
  uint16_t* ui_copy_ = nullptr;
};

}  // namespace p4_dsi_camera_presenter

#endif  // CONFIG_IDF_TARGET_ESP32P4
