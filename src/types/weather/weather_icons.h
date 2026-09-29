#pragma once

#include <Arduino.h>
#include <lvgl.h>

// Filled, multi-color weather icons (tools/weather-icons/parts.mjs) for the
// weather tile and popup. A weather icon label uses the weather icon font,
// which falls back to the MDI font, with label recoloring: a known weather
// icon draws its colored layers, every other icon its plain MDI glyph in the
// label color.
namespace weather_icons {

// How a weather icon label draws a weather icon.
enum class Style : uint8_t {
  Colored,  // filled layers in their weather colors
  Single,   // filled layers in the label color (an icon color the user chose)
  Outline,  // the white MDI outline (Weather setting "Colored weather icons" off)
};

// Icon font of this layout: the MDI icon size with the weather layers.
const lv_font_t* font();

// Gives an icon label the weather icon font and recoloring.
void style_label(lv_obj_t* label);

// Label text for an MDI icon name in `style`; names without weather layers
// always show their MDI glyph. Empty when the name has no glyph.
String text(const String& icon_name, Style style = Style::Colored);

// Weather color of an icon name (0xRRGGBB), the label color of a colored
// icon; 0 for names without weather layers.
uint32_t tint(const String& icon_name);

// The night variant of a day icon: partly cloudy shows the moon behind the
// cloud and sunny the clear night. Other names are returned unchanged.
String at_night(const String& icon_name);

// Local sunrise and sunset per day from the bridge weather payload:
//   "sun":[{"d":"YYYY-MM-DD","r":<sunrise minute>,"s":<sunset minute>}, ...]
// with "up":true/false instead of r/s for polar day/night.
struct SunDay {
  char date[11] = "";
  int16_t rise = -1;
  int16_t set = -1;
  int8_t up = -1;  // polar: 1 day, 0 night; -1 uses rise/set
};
struct SunTimes {
  static constexpr uint8_t kMaxDays = 8;
  SunDay days[kMaxDays];
  uint8_t count = 0;
};

// Reads the "sun" list of a weather payload; false (no days) without it.
bool parse_sun(const char* json, SunTimes& out);

// True when local `date` (YYYY-MM-DD) at `minute` after midnight is night.
// Without times for that date it is day.
bool is_night(const SunTimes& sun, const char* date, int minute);

// The icon for the current local time: its night variant after sunset.
String for_now(const String& icon_name, const SunTimes& sun);

// A rule forced (or released) the icon color of a weather icon label: shows
// its layers in that single color (or in the weather colors again). Labels
// without weather layers keep their text.
void follow_icon_color(lv_obj_t* label, bool forced);

}  // namespace weather_icons
