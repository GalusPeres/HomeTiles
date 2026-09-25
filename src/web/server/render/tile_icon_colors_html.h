#pragma once

#include <Arduino.h>

// Web Admin tile editor block for per-tile icon colors: the fixed icon color,
// the color bar for numeric states, up to six state colors for text states
// and the Binary sensor On/Off colors. One block per tab, shown by the editor
// only for Sensor, Number, Select, Date/Time, Binary sensor and Energy tiles.
void append_tile_icon_color_fields_html(String& html, const String& tab_id);
void append_tile_icon_color_fixed_html(String& html, const String& tab_id);
