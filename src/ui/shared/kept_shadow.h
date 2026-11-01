#pragma once

// A popup card's shadow cost 17-20 ms in every popup frame on the V2 (b603
// FrameProbe: 25 shadow tasks, 0.7 ms each): lv_draw_sw_box_shadow copies its
// cached corner blur for every band the shadow touches, mirrors it, and
// blends the shadow's sides row by row, two blends for every row of a band.
//
// A shadow drawn here keeps LVGL's own corner blur (its shadow cache, taken
// once LVGL has computed it) as four corner images and two side strips, and
// draws every part of a band in one blend, with the same values and the same
// blends LVGL gives each pixel. Shadows over a translucent background, with
// a corner too large for LVGL's cache or with a visible centre stay LVGL's.
// Drawn by the software draw unit's dispatch (rounded_corners.cpp).

#include <stdint.h>

#include "lvgl.h"

namespace kept_shadow {

struct Set;

// The kept shadow for a box shadow task, or null when LVGL draws it.
const Set* find(const lv_draw_task_t* task);
// Draws the task's part in its clip area (the layer buffer is allocated).
void draw(lv_draw_task_t* task, const Set& set);

struct Stats {
  uint32_t drawn;   // shadow tasks drawn from kept corners
  uint32_t lvgl;    // shadow tasks left to LVGL
  uint32_t sets;    // shadows kept now
  uint32_t bytes;   // their bytes
  uint32_t built;   // shadows kept since boot
};
Stats stats();

}  // namespace kept_shadow
