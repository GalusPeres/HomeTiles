#pragma once

#include "src/tiles/runtime/tile_renderer.h"
#include "src/types/switch/state.h"

lv_obj_t* render_switch_tile(lv_obj_t* parent, int col, int row, const Tile& tile, uint8_t index, GridType grid_type);

// Shows a Switch tile's state on its widgets: the icon color (`icon_rgb`,
// grey while off) and, for the header layouts, the state line and the bar.
void switch_tile_show_state(SwitchTileWidgets& widgets, const Tile& tile, const SwitchState& state,
                            uint32_t icon_rgb);
