#pragma once

#include <lvgl.h>

#include "src/devices/device_select.h"
#include "src/tiles/runtime/tile_renderer_fonts.h"

// Geometry and fonts of the weather tile: renderer.cpp builds it, the state
// update in tile_renderer.cpp fills it, and the Web Admin preview draws it
// from the same values (web_scripts.cpp).
namespace weather_tile {

// The forecast row sits this far above its default place.
#if defined(DEVICE_WAVESHARE_TOUCH_LCD_1280X800) || \
    defined(DEVICE_GUITION_JC8012P4A1) || \
    defined(DEVICE_GUITION_JC8012P4A1_V2)
constexpr lv_coord_t kForecastYOffset = -10;
#elif defined(DEVICE_LAYOUT_1024X600)
constexpr lv_coord_t kForecastYOffset = -5;
#else
constexpr lv_coord_t kForecastYOffset = 0;
#endif

// A forecast column is one cell high plus this headroom for its day label;
// the labels' tops within the column's content area.
constexpr lv_coord_t kForecastHeadroom = tile_layout::scale(52);
constexpr lv_coord_t kForecastDayTop = kForecastHeadroom - tile_layout::scale(33);
constexpr lv_coord_t kForecastIconTop = kForecastHeadroom - tile_layout::scale(8);
constexpr lv_coord_t kForecastTempTop = kForecastHeadroom + tile_layout::scale(54);
constexpr lv_coord_t kForecastLowTop = kForecastTempTop + tile_layout::scale(30);

// The unit beside a forecast temperature starts this far below the value's
// top edge; the compact layouts share the top edge.
#if defined(DEVICE_LAYOUT_1024X600) || defined(DEVICE_LAYOUT_480X480)
constexpr lv_coord_t kUnitYOffset = 0;
#else
constexpr lv_coord_t kUnitYOffset = 5;
#endif

// The condition | temperature row: its gap, and its offset below the middle
// of the (top) cell, like the Sensor value.
constexpr lv_coord_t kValueGap = tile_layout::scale(14);
constexpr lv_coord_t kValueDy = tile_layout::scale(28);

inline const lv_font_t* value_font() { return FONT_VALUE; }

inline const lv_font_t* forecast_font() {
#if defined(DEVICE_LAYOUT_1024X600)
  return tile_layout::content_font_20();
#else
  return tile_layout::content_font_24();
#endif
}

inline const lv_font_t* forecast_day_font() {
#if defined(DEVICE_LAYOUT_1024X600)
  return tile_layout::content_font_20();
#else
  return FONT_TITLE;
#endif
}

// The space before the forecast temperatures' unit.
inline const char* unit_gap() {
#if defined(DEVICE_LAYOUT_1024X600)
  return " ";
#else
  return "\xE2\x80\x89";
#endif
}

inline const lv_font_t* unit_font() {
#if defined(DEVICE_LAYOUT_1024X600)
  return LV_FONT_DEFAULT;
#else
  return FONT_SMALL;
#endif
}

}  // namespace weather_tile
