#pragma once

#include <Arduino.h>

#include "src/fonts/ui_fonts.h"
#include "src/tiles/runtime/compact_sensor_layout.h"
#include "src/tiles/runtime/tile_renderer_fonts.h"
#include "src/web/server/web_admin_utils.h"

// The value size choices (Tile::sensor_value_font 0..5, tile_config.h) as
// this panel draws them, as data attributes of a value size select:
// `data-sizes` on a full tile whose Default is `full_default`,
// `data-compact-sizes` in the header row beside the disc
// (compact_sensor_layout: half-height tiles, the Sensor graph, a Switch one
// row high), and the Default word. The editor names each choice by its size
// and offers each size once (syncCompactValueFontOptions in
// web/admin/tiles/layout.js). The lists named the 1280x800 sizes: a graph
// tile's "28 (Default)" was its 20 px title size, so 20 looked the same and
// 24 bigger (user 2026-10-27).
inline void append_value_size_attrs(String& html, const lv_font_t* full_default, const char* default_label) {
  html += " data-sizes=\"";
  for (uint8_t choice = 0; choice <= SENSOR_VALUE_FONT_MAX; ++choice) {
    if (choice) html += ',';
    html += ui_font_size_of(tile_layout::value_font_for_choice(choice, full_default));
  }
  html += "\" data-compact-sizes=\"";
  for (uint8_t choice = 0; choice <= SENSOR_VALUE_FONT_MAX; ++choice) {
    if (choice) html += ',';
    html += compact_sensor_layout::value_size(choice);
  }
  html += "\" data-default-label=\"";
  appendHtmlEscaped(html, default_label);
  html += '"';
}

// The Default option's text on a full tile: its size and the Default word.
inline void append_value_size_default(String& html, const lv_font_t* full_default, const char* default_label) {
  html += ui_font_size_of(full_default);
  html += " (";
  appendHtmlEscaped(html, default_label);
  html += ')';
}
