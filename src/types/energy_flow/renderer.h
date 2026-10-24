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
