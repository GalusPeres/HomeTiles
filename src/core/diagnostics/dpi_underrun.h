#pragma once

#include <stdint.h>

// Blue flashes on P4 panels (user 2026-10-31, b642: three short ones after a
// start; 2026-10-30 on Web Admin tab switches). A blue flash is the DSI bridge
// running out of pixels: the frame buffer could not be read from PSRAM fast
// enough (DPI underrun). ESP-IDF's bridge interrupt reports it only through
// esp_rom_printf, and the ROM console never reaches the USB CDC log of these
// builds, so no log ever showed one. Test builds take the underrun bit over:
// an esp_timer polls the sticky raw bit and logs each burst once, after it has
// been quiet for kQuietMs, with its length and the loop step that was running.
namespace dpi_underrun {

constexpr uint32_t kPollMs = 5;
constexpr uint32_t kQuietMs = 100;

// Polls of the underrun bit grouped into bursts: one log line per burst.
struct Burst {
  uint32_t first_ms = 0, last_ms = 0, hits = 0;
  const char* step = nullptr;
  bool open = false;

  // One poll. True when an open burst has stayed quiet for kQuietMs: the
  // caller logs it; the next hit opens a new burst.
  bool poll(bool underrun, uint32_t now_ms, const char* step_now) {
    if (underrun) {
      if (!open) {
        open = true;
        first_ms = now_ms;
        hits = 0;
        step = step_now;
      }
      last_ms = now_ms;
      ++hits;
      return false;
    }
    if (open && now_ms - last_ms >= kQuietMs) {
      open = false;
      return true;
    }
    return false;
  }
};

}  // namespace dpi_underrun

#if (defined(HOMETILES_ISSUE38_BETA) || defined(HOMETILES_TEST_BETA)) && defined(ESP_PLATFORM)
#include <sdkconfig.h>
#if CONFIG_IDF_TARGET_ESP32P4
#define HOMETILES_DPI_UNDERRUN 1
#endif
#endif

#if defined(HOMETILES_DPI_UNDERRUN)
#include <Arduino.h>
#include <esp_timer.h>
#include <soc/mipi_dsi_bridge_struct.h>

// Declared here instead of including loop_stall.h: the sketch includes both,
// and the nested include reaches loop_stall.h by a second path, which GCC on
// Windows takes for another file (#pragma once does not hold, b643 build).
namespace loop_stall {
const char* currentStepName();
}  // namespace loop_stall

namespace dpi_underrun {

inline Burst burst;

// esp_timer task: keeps running while the loop task is blocked.
inline void poll(void*) {
  const bool hit = MIPI_DSI_BRIDGE.int_raw.underrun_int_raw;
  if (hit) MIPI_DSI_BRIDGE.int_clr.underrun_int_clr = 1;
  if (!burst.poll(hit, millis(), loop_stall::currentStepName())) return;
  Serial.printf("[Display] DPI underrun (blue flash): %u ms long, %u polls, at %u ms, loop step %s\n",
                static_cast<unsigned>(burst.last_ms - burst.first_ms + kPollMs),
                static_cast<unsigned>(burst.hits), static_cast<unsigned>(burst.first_ms),
                burst.step ? burst.step : "?");
}

// After the DPI panel is running (BoardHAL::init).
inline void begin() {
  static esp_timer_handle_t timer = nullptr;
  if (timer) return;
  // ESP-IDF's bridge interrupt would clear the bit first and print to the ROM
  // console only; without it the raw bit still sets and the poll clears it.
  MIPI_DSI_BRIDGE.int_ena.underrun_int_ena = 0;
  MIPI_DSI_BRIDGE.int_clr.underrun_int_clr = 1;
  esp_timer_create_args_t args = {};
  args.callback = &poll;
  args.dispatch_method = ESP_TIMER_TASK;
  args.name = "dpi_underrun";
  args.skip_unhandled_events = true;
  esp_err_t err = esp_timer_create(&args, &timer);
  if (err == ESP_OK) err = esp_timer_start_periodic(timer, kPollMs * 1000ULL);
  if (err != ESP_OK) {
    Serial.printf("[Display] DPI underrun watch unavailable: %s\n", esp_err_to_name(err));
    return;
  }
  Serial.printf("[Display] DPI underrun watch active (poll %u ms)\n", static_cast<unsigned>(kPollMs));
}

}  // namespace dpi_underrun

#else

namespace dpi_underrun {
inline void begin() {}
}  // namespace dpi_underrun

#endif
