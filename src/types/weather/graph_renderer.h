#pragma once

#include <lvgl.h>

#include "src/tiles/runtime/tile_renderer.h"

// A two-day weather graph tile: one tile shows today (from the current hour)
// or tomorrow, the pair is linked by the Web Admin but each tile renders on
// its own. The graph is an LVGL chart (temperature) with a condition band
// underneath; no numeric precipitation is used for the Yandex integration.
struct WeatherGraphWidgets {
  lv_obj_t* chart = nullptr;
  lv_chart_series_t* series = nullptr;
  lv_obj_t* title_label = nullptr;
  lv_obj_t* placeholder = nullptr;
  // Every three hours of the 00-23 axis: the hour under the scale...
  lv_obj_t* hour_labels[8] = {};
  // ...and the temperature above the line.
  lv_obj_t* temp_labels[8] = {};
  lv_obj_t* band = nullptr;
  lv_obj_t* band_cells[24] = {};
  // One label per contiguous precipitation episode, written into the band.
  lv_obj_t* band_labels[6] = {};
  // The current hour on the today half: a vertical dashed line with its dot
  // on the curve (LVGL chart cursor, LV_PART_CURSOR).
  lv_chart_cursor_t* now_cursor = nullptr;
  uint32_t last_hash = 0;
};

lv_obj_t* render_weather_graph_tile(lv_obj_t* parent, int col, int row,
                                    const Tile& tile, uint8_t index,
                                    GridType grid_type);

void update_weather_graph_tile_state(GridType grid_type, uint8_t grid_index,
                                     const char* payload);

// Shared graph scale (tenths of a degree) across today's remaining hours and
// tomorrow, used by both halves of the pair.
void weather_graph_scale(const char* payload, int32_t& min_tenths,
                         int32_t& max_tenths, bool& has_temp);

// Append cached past hours missing from the payload as "src":"hist" entries,
// so the Web Admin preview draws the same whole day as the panel.
void weather_graph_append_cache(String& payload, const char* entity_id);
