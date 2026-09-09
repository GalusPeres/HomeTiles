#pragma once

#include "src/devices/common/p4_dsi_camera_presenter.h"

#if defined(CONFIG_IDF_TARGET_ESP32P4)

namespace p4_dsi_ui_ppa {

enum class Result { CpuFallback, Drawn, Failed };

// Shared Waveshare 8-inch UI rotation and recovery policy. Board drivers own
// panel initialization and the existing CPU fallback for small dirty regions.
class Client {
 public:
  explicit Client(const char* device_name) : device_name_(device_name) {}
  void init();
  void pauseFor(uint32_t duration_ms);
  bool cooldownActive() const;
  void noteFault();
  void noteSuccess() { consecutive_faults_ = 0; }
  p4_dsi_camera_presenter::PpaRuntime runtime(void (*note_fault)()) const;

  Result rotate(p4_dsi_camera_presenter::Presenter& presenter,
                int32_t panel_width, int32_t panel_height,
                int32_t x, int32_t y, int32_t w, int32_t h,
                const uint16_t* data, uint8_t rotation);

 private:
  void reset(bool initial = false);
  void scheduleReinit();

  const char* device_name_;
  ppa_client_handle_t handle_ = nullptr;
  SemaphoreHandle_t done_ = nullptr;
  bool async_ready_ = false;
  bool reset_pending_ = false;
  uint32_t cooldown_until_ms_ = 0;
  uint32_t reinit_at_ms_ = 0;
  uint8_t consecutive_faults_ = 0;
};

}  // namespace p4_dsi_ui_ppa

#endif
