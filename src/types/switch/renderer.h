#pragma once

#include "src/tiles/runtime/tile_renderer.h"
#include "src/types/switch/state.h"

lv_obj_t* render_switch_tile(lv_obj_t* parent, int col, int row, const Tile& tile, uint8_t index, GridType grid_type);

// Whether the tile shows its own on/off right now instead of the reported
// state (a dimmer held or released moments ago, a tile toggled moments ago),
// and which. Icon, circle and tile tint follow it.
bool switch_tile_held_on(const SwitchTileWidgets& widgets, bool& on);

// Shows a Switch tile's state on its widgets: the icon color (`icon_rgb`,
// grey while off) and, for the header layouts, the state line and the bar.
void switch_tile_show_state(SwitchTileWidgets& widgets, const Tile& tile, const SwitchState& state,
                            uint32_t icon_rgb);
