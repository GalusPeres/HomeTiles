#pragma once

#include <Arduino.h>
#include <lvgl.h>

#include "src/tiles/runtime/tile_renderer.h"

// The Lock, Alarm panel and Fan tile (user 01.10., simulator
// build/ha-dummy-sim/panel/panel_ui.inc): the Switch tile's header (corner
// disc, title and state; the Sensor header from 1.5 rows; the Sensor compact
// layout at half height) with a control bar in the Climate pill's box
// (level_bar.h). Lock: a toggle (right = locked), Unlock | Lock buttons for
// an unknown or jammed lock. Alarm: Home Assistant's mode bar (Disarm left),
// only Disarm while arming, pending or triggered. Fan: a dimmer, segments for
// up to four speeds or a toggle without speeds. The bar owns its touches and
// never opens the popup; the rest of the card does.
//
// Every rendered tile keeps a small view owned by its card. Views are found
// by entity, so a hidden cached folder grid follows the state too and no
// per-slot arrays, snapshots or resets are needed.
lv_obj_t* render_device_tile(lv_obj_t* parent, int col, int row, const Tile& tile, uint8_t index,
                             GridType grid_type);

// Shows the current state on every tile of `entity` ("*": every tile).
void device_tiles_refresh(const String& entity);
// True while `card` is a rendered Lock, Alarm panel or Fan tile (compared,
// never dereferenced): a popup reads its opening card only while it lives.
bool device_tile_alive(lv_obj_t* card);
