#pragma once

#include <stdint.h>

// A full frame on the V2 is drawn in 29 bands of 28 rows; after each band the
// PPA rotated it into the framebuffer while the CPU waited, 0.8 ms per band
// and 22-24 ms per frame (V2 b600 FrameProbe "flush"). A board that registers
// a backend lets that copy run on while LVGL walks the objects of the next
// band; LVGL's draw dispatch then waits for the copy before it writes the
// first pixel of that band (ready()), so the band buffer is never drawn into
// while the copy still reads it. The last band of a frame is copied
// synchronously as before, and every flush first completes a copy still under
// way (settle()). Header only for the state, so host tests can link the draw
// code without a board.
namespace display_flush_gate {

struct Backend {
  // Starts copying the band; true when the copy runs on after this returns.
  bool (*begin)(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* data);
  // The started copy finished in hardware (does not wait).
  bool (*done)();
  // Waits for the started copy and completes it (cache, DMA2D lease).
  void (*finish)();
};

inline Backend g_backend = {nullptr, nullptr, nullptr};
// Set once the draw dispatch goes through ready() (install_dispatch()).
inline bool g_dispatch_gated = false;
inline bool g_pending = false;

inline void set_backend(const Backend& backend) { g_backend = backend; }

inline bool armed() {
  return g_dispatch_gated && g_backend.begin && g_backend.done && g_backend.finish;
}

// For the flush: starts the band's copy in the background when the board can.
inline bool begin(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t* data) {
  if (!armed() || g_pending || !g_backend.begin(x, y, w, h, data)) return false;
  g_pending = true;
  return true;
}

// For the draw dispatch: false while the last band's copy still reads the
// draw buffer; a finished copy is completed here.
inline bool ready() {
  if (!g_pending) return true;
  if (!g_backend.done()) return false;
  g_backend.finish();
  g_pending = false;
  return true;
}

// Waits for a copy still under way (before any other flush).
inline void settle() {
  if (!g_pending) return;
  g_backend.finish();
  g_pending = false;
}

// Puts ready() in front of LVGL's software draw unit (one unit only). Called
// once LVGL has its draw unit, after the board registered its backend.
void install_dispatch();

}  // namespace display_flush_gate
