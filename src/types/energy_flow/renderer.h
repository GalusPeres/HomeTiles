#pragma once

#include <lvgl.h>
#include "src/tiles/config/tile_config.h"
#include "src/tiles/runtime/tile_renderer.h"
#include "src/types/energy_flow/flow_updates.h"

lv_obj_t* render_energy_flow_tile(lv_obj_t* parent,
                                  int col,
                                  int row,
                                  const Tile& tile,
                                  uint8_t index,
                                  GridType grid_type);

// A page, folder grid or the tiles' tab was just shown (tab_tiles_unified,
// ui_manager): a flow on it moves with the next frame, and its first frames
// are logged once (when they came and what they found).
void energy_flow_shown(lv_obj_t* shown);
// A finger came down on `pressed` (ui_manager): on a node or value that
// opens a numeric sensor's popup, its history is asked now
// (sensor_popup_prefetch_history).
void energy_flow_touch_down(lv_obj_t* pressed);
