#pragma once

#include "src/tiles/runtime/tile_renderer.h"
#include "src/types/switch/state.h"

lv_obj_t* render_switch_tile(lv_obj_t* parent, int col, int row, const Tile& tile, uint8_t index, GridType grid_type);

// Whether the tile shows its own on/off right now instead of the reported
// state (a dimmer held or released moments ago, a tile toggled moments ago),
// and which. Icon, circle and tile tint follow it.
bool switch_tile_held_on(const SwitchTileWidgets& widgets, bool& on);

// The Light popup opened from this tile changes its light: `value` its level
// (0 = off) when `level`, else 1 or 0 for on or off. The tile keeps that
// value against Home Assistant's echoes of the commands in between (a light
// with a transition reports the steps on its way) during the gesture and
// kRemoteBlockMs after its `released` end, then shows the reported state,
// like a release of its own dimmer. The tile ran through those echoes behind
// the popup, 85 -> 71 -> 30 % (user video 2026-10-27).
void switch_tile_hold_from_popup(GridType grid_type, uint8_t index, uint8_t value, bool level, bool released);

// Shows a Switch tile's state on its widgets: the icon color (`icon_rgb`,
// grey while off) and, for the header layouts, the state line and the bar.
void switch_tile_show_state(SwitchTileWidgets& widgets, const Tile& tile, const SwitchState& state,
                            uint32_t icon_rgb);
