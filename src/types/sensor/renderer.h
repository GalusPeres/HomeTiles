#pragma once

#include "src/tiles/runtime/tile_renderer.h"

lv_obj_t* render_sensor_tile(lv_obj_t* parent, int col, int row, const Tile& tile, uint8_t index, GridType grid_type);

// A finger came down on `pressed` (ui_manager, before it is a tap or a
// swipe): on a sensor tile whose popup shows a numeric graph, its history is
// asked now (sensor_popup_prefetch_history).
void sensor_tile_touch_down(lv_obj_t* pressed);
