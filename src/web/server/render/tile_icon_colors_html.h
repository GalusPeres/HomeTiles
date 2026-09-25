#pragma once

#include <Arduino.h>

// Web Admin tile editor block for per-tile icon colors (fixed icon color and
// up to three color rules). One block per tab, shown by the editor only for
// Sensor, Number, Select, Date/Time, Binary sensor and Energy tiles.
void append_tile_icon_color_fields_html(String& html, const String& tab_id);
