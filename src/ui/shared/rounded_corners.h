#pragma once

// LVGL draws a rounded rectangle's corners row by row: for every row of a
// corner it builds a coverage mask from the circle and blends that row alone.
// A tile card (radius 31) with its 1 px frame line costs 4 x 31 such rows for
// the fill and as many for the line in every draw band they cross, about 6 us
// per row (V2 b592 FrameProbe: card.fill 9.0 + card.border 12.3 ms of a
// 114 ms page turn, user 2026-10-28: make every tile cheaper).
//
// attach() makes a card or an icon disc draw the same shapes with the same
// pixels in fewer steps: when LVGL adds the object's main fill or border task,
// the task is marked, and the software draw unit's dispatch draws a marked
// task itself: its straight parts as plain blends (exactly the unmasked parts
// LVGL blends itself) and each corner in one blend with the corner's kept A8
// coverage. The coverage is computed once by LVGL's own radius masks, so
// every corner pixel gets the mask value and blend LVGL would give it. It
// stays one task: b593 added the parts as tasks of their own, and creating
// those (~12 us each) cost what the corners saved. Gradients, drop shadows,
// partial sides and translucent borders keep LVGL's own drawing.

#include <stdint.h>

#include "lvgl.h"

namespace rounded_corners {

void attach(lv_obj_t* obj);

// attach() for `root` and every rounded object inside it (a tile card's
// control bars, pills, buttons and icon discs; V2 b595: a 287x68 bar with
// radius = half its height masked every row, 0.8-1.9 ms each).
void attach_tree(lv_obj_t* root);

struct Stats {
  uint32_t fills;     // fill tasks drawn from kept corners
  uint32_t borders;   // border tasks drawn from kept corners
  uint32_t drawn;     // marked tasks the dispatch drew
  uint32_t masks;     // corner sets kept now
  uint32_t bytes;     // their coverage bytes
  uint32_t built;     // corner sets computed since boot
  uint32_t declined;  // tasks left to LVGL because every set was in use this frame
};
Stats stats();

}  // namespace rounded_corners
