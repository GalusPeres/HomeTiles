#pragma once

#include <lvgl.h>
#include "src/tiles/config/tile_config.h"
#include "src/tiles/runtime/tile_renderer.h"

lv_obj_t* render_energy_tile(lv_obj_t* parent,
                             int col,
                             int row,
                             const Tile& tile,
                             uint8_t index,
                             GridType grid_type);

struct EnergyEntryData;

// A new day answer for an Energy source: the tiles showing its bars take the
// hours and redraw (sensor_display_mode 2).
void energy_tile_bars_update(const EnergyEntryData& entry);
