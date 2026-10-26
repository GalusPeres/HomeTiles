#include "src/types/sensor/web_scripts.h"

#include "src/ui/shared/value_unit.h"

// The editable JavaScript lives in src/web/assets/admin.js and is served as
// one precompressed, cacheable firmware asset. The page gets the panel's
// unit font sizes, so the preview picks a unit's size like the panel
// (value_unit.h; sensor admin-format.js valueUnitStyle).
void append_sensor_scripts(String& html) {
  uint8_t count = 0;
  const uint8_t* sizes = value_unit::unit_font_sizes(count);
  html += "  <script>\n  const VALUE_UNIT_FONTS = Object.freeze([";
  for (uint8_t i = 0; i < count; ++i) {
    if (i) html += ", ";
    html += String(sizes[i]);
  }
  html += "]);\n  </script>\n";
}
