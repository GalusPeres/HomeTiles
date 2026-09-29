#pragma once

#include <Arduino.h>
#include <lvgl.h>

// Filled, multi-color weather icons (tools/weather-icons/parts.mjs) for the
// weather tile and popup. A weather icon label uses the weather icon font,
// which falls back to the MDI font, with label recoloring: a known weather
// icon draws its colored layers, every other icon its plain MDI glyph in the
// label color.
namespace weather_icons {

// Icon font of this layout: the MDI icon size with the weather layers.
const lv_font_t* font();

// Gives an icon label the weather icon font and recoloring.
void style_label(lv_obj_t* label);

// Label text for an MDI icon name: the colored layers of a weather icon,
// otherwise the MDI glyph; empty when the name has no glyph.
String text(const String& icon_name);

}  // namespace weather_icons
