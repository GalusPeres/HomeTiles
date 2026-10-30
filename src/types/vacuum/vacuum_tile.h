#pragma once

#include <Arduino.h>
#include <lvgl.h>

#include "src/tiles/runtime/tile_renderer.h"

// The Vacuum tile (build/design-mockups/vacuum): the Lock/Alarm/Fan tiles'
// header (corner disc, title, state "Docked · 84 %"; from 1.5 rows the state
// centred like theirs; the Sensor compact layout at half height) with its
// controls in the level bar's place. One column: the main button (Clean, or
// Pause while cleaning) across it. Two columns: Dock, the main button, Stop,
// the same row on every size; two columns and two rows put the map between
// the header (beside the disc) and that row. Every button runs only what the
// vacuum's feature bits allow; the rest of the card opens the popup.
//
// Views are found by entity, so a hidden cached folder grid follows the
// state too (like the Lock, Alarm panel and Fan tiles).
lv_obj_t* render_vacuum_tile(lv_obj_t* parent, int col, int row, const Tile& tile, uint8_t index,
                             GridType grid_type);

// Shows the current state on every tile of `entity` ("*": every tile).
void vacuum_tiles_refresh(const String& entity);
