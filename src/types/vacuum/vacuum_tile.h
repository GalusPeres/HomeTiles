#pragma once

#include <Arduino.h>
#include <lvgl.h>

#include "src/tiles/runtime/tile_renderer.h"

// The Vacuum tile (build/design-mockups/vacuum): the Switch tile's header
// (corner disc, title, state "Cleaning · 84 %"; the Sensor compact layout at
// half height) with its controls in the level bar's box. One column: the
// main button (Clean, or Pause while cleaning) across the box. Two columns:
// Dock, the main button, Stop. Two columns and two rows: the map centred
// below the header, Dock left and the main button as a circle right. Every
// button runs only what the vacuum's feature bits allow; the rest of the
// card opens the popup.
//
// Views are found by entity, so a hidden cached folder grid follows the
// state too (like the Lock, Alarm panel and Fan tiles).
lv_obj_t* render_vacuum_tile(lv_obj_t* parent, int col, int row, const Tile& tile, uint8_t index,
                             GridType grid_type);

// Shows the current state on every tile of `entity` ("*": every tile).
void vacuum_tiles_refresh(const String& entity);
