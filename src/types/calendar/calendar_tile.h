#pragma once

#include <Arduino.h>
#include <lvgl.h>

#include "src/tiles/runtime/tile_renderer.h"

// The Calendar tile (mockup build/design-mockups/calendar, 2026-10-24): up to
// four Home Assistant calendars in their own colors, the Sensor tile's header
// (corner disc, title top right) above what the size allows:
//   half height        the next event like a Sensor chip (when, title)
//   below two rows     "Up next": the next event with its time and countdown
//   two rows and more  the agenda: the coming days with their events
//   view Week          seven days with today marked and a dot per calendar
//   view Month         the month grid (panels larger than 480x480, at least
//                      2x2; else the agenda)
// The card opens the popup. Every rendered tile keeps a small view owned by
// its card and found by calendar, like the device tiles, so hidden cached
// folder grids follow new events too. A timer redraws them when the minute
// or the day changes.
lv_obj_t* render_calendar_tile(lv_obj_t* parent, int col, int row, const Tile& tile, uint8_t index,
                               GridType grid_type);

// Shows the current events on every tile of `entity` ("*": every tile).
void calendar_tiles_refresh(const String& entity);
// True while `card` is a rendered Calendar tile (compared, never
// dereferenced).
bool calendar_tile_alive(lv_obj_t* card);
