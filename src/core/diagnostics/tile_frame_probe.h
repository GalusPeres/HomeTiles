#pragma once

#include <lvgl.h>
#include <stdint.h>

// What a large frame costs per tile (user 2026-10-28: a page turn to a full
// Home builds up visibly; measure before changing anything). Every frame
// whose dirty areas cover at least two fifths of the screen logs one line,
// at most one per second: the frame's time, its flushes, the time inside
// the tiles and the tiles that took longest (their own drawing and their
// children, summed over all bands of the frame). Smaller frames cost one
// flag check per draw event.
//
// Measurement rounds only: it times every other large frame (S3 b605: Home
// 246 ms timed, 210-225 ms lite) and the test builds are judged by feel
// against the releases (user 2026-10-28: "die alte Version war ueberall
// schneller"). A measurement round turns it on here.
// #define HOMETILES_FRAME_PROBE 1
namespace tile_frame_probe {

// Called for every rendered tile card (render_tile); `index` is its slot.
void watch(lv_obj_t* card, uint8_t index, uint8_t type);

}  // namespace tile_frame_probe
