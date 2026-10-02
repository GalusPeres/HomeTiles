#include "src/types/weather/web_scripts.h"

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/fonts/ui_fonts.h"
#include "src/tiles/config/tile_config.h"
#include "src/types/weather/tile_layout.h"
#include "src/types/weather/weather_icons.h"
#include "src/types/weather/widgets.h"

namespace {

// Pixel size of a weather tile font; the 1024x600 forecast unit uses LVGL's
// default font (Montserrat 14).
int weather_font_px(const lv_font_t* font) {
  struct Size {
    const lv_font_t* font;
    int px;
  };
  static const Size kSizes[] = {
#if defined(DEVICE_LAYOUT_480X480)
      {&ui_font_12, 12}, {&ui_font_14, 14},
#endif
      {&ui_font_16, 16}, {&ui_font_20, 20}, {&ui_font_24, 24}, {&ui_font_28, 28},
      {&ui_font_32, 32}, {&ui_font_40, 40}, {&ui_font_48, 48},
  };
  for (const Size& size : kSizes) {
    if (size.font == font) return size.px;
  }
  return font == LV_FONT_DEFAULT ? 14 : 20;
}

// {px, line, base}: a label is as tall as the line height, its baseline
// base_line above the bottom.
void append_weather_font(String& html, const char* key, const lv_font_t* font) {
  char text[96];
  snprintf(text, sizeof(text), "    %s: {px: %d, line: %d, base: %d},\n", key,
           weather_font_px(font), static_cast<int>(font->line_height),
           static_cast<int>(font->base_line));
  html += text;
}

void append_weather_js_string(String& html, const String& value) {
  html += "'";
  for (size_t index = 0; index < value.length(); ++index) {
    const char c = value[index];
    switch (c) {
      case '\\': html += "\\\\"; break;
      case '\'': html += "\\'"; break;
      case '\r': break;
      case '\n': html += "\\n"; break;
      case '<': html += "\\x3c"; break;
      default: html += c; break;
    }
  }
  html += "'";
}

}  // namespace

// The editable JavaScript lives in src/web/assets/admin.js and is served as
// one precompressed, cacheable firmware asset. The weather tile preview takes
// the device's own texts: condition labels, short weekdays and "Today".
void append_weather_scripts(String& html) {
  const char* language = configManager.getConfig().language;
  html += "  <script>\n  const WEATHER_I18N = Object.freeze({\n    conditions: Object.freeze({\n";
  static const char* kConditions[] = {
      "clear-night", "cloudy", "exceptional", "fog", "hail",
      "lightning", "lightning-rainy", "partlycloudy", "pouring",
      "rainy", "snowy", "snowy-rainy", "sunny", "windy",
      "windy-variant"};
  for (const char* condition : kConditions) {
    html += "      '";
    html += condition;
    html += "': ";
    append_weather_js_string(html, i18n::weather_condition_label(language, condition));
    html += ",\n";
  }
  // Sunday first, like weather_weekday_short().
  html += "    }),\n    weekdaysShort: Object.freeze([";
  for (int day = 0; day < 7; ++day) {
    if (day) html += ", ";
    append_weather_js_string(html, i18n::locale(language).weather_weekdays_short[day]);
  }
  html += "]),\n    today: ";
  append_weather_js_string(html, i18n::weather_today_label(language));
  html += "\n  });\n";

  // The tile geometry in display pixels (renderer.cpp, tile_layout.h); the
  // preview scales it like every other tile.
  html += "  const WEATHER_TILE_LAYOUT = Object.freeze({\n";
  char text[160];
  snprintf(text, sizeof(text),
           "    cellW: %d, cellH: %d, gap: %d, padH: %d, padV: %d, colW: %d,\n",
           GRID_CELL_W, GRID_CELL_H, GRID_GAP, static_cast<int>(tile_layout::scale_480(20)),
           static_cast<int>(tile_layout::scale_480(24)), static_cast<int>(WEATHER_FORECAST_COL_W));
  html += text;
  snprintf(text, sizeof(text),
           "    headroom: %d, yOffset: %d, dayTop: %d, iconTop: %d, tempTop: %d, lowTop: %d,\n",
           static_cast<int>(weather_tile::kForecastHeadroom),
           static_cast<int>(weather_tile::kForecastYOffset),
           static_cast<int>(weather_tile::kForecastDayTop),
           static_cast<int>(weather_tile::kForecastIconTop),
           static_cast<int>(weather_tile::kForecastTempTop),
           static_cast<int>(weather_tile::kForecastLowTop));
  html += text;
  snprintf(text, sizeof(text), "    unitDy: %d, valueGap: %d, valueDy: %d, minConditionRoom: %d,\n",
           static_cast<int>(weather_tile::kUnitYOffset), static_cast<int>(weather_tile::kValueGap),
           static_cast<int>(weather_tile::kValueDy), static_cast<int>(tile_layout::scale(60)));
  html += text;
  {
    // The weather icon font copies the MDI line metrics: its em box (the
    // 24-unit grid) starts this far below the label top.
#if defined(DEVICE_LAYOUT_1024X600)
    constexpr int kIconPx = 40;
#elif defined(DEVICE_LAYOUT_480X480)
    constexpr int kIconPx = 32;
#else
    constexpr int kIconPx = 48;
#endif
    const lv_font_t* icon_font = weather_icons::font();
    snprintf(text, sizeof(text), "    iconPx: %d, iconEmDy: %d,\n", kIconPx,
             static_cast<int>(icon_font->line_height - icon_font->base_line) - kIconPx * 21 / 24);
    html += text;
  }
  append_weather_font(html, "value", weather_tile::value_font());
  append_weather_font(html, "day", weather_tile::forecast_day_font());
  append_weather_font(html, "temp", weather_tile::forecast_font());
  append_weather_font(html, "unit", weather_tile::unit_font());
  // A thin space before the unit, a normal space on 1024x600
  // (format_weather_temp_unit in tile_renderer.cpp).
#if defined(DEVICE_LAYOUT_1024X600)
  html += "    unitGap: ' ',\n";
#else
  html += "    unitGap: '\\u2009',\n";
#endif
  html += "  });\n  </script>\n";
}
