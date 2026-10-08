#pragma once

#include <stdint.h>
#include <lvgl.h>

#include "src/devices/device.h"
#include "src/devices/device_select.h"

struct WeatherForecastWidgets {
  lv_obj_t* day_label = nullptr;
  lv_obj_t* sep_label = nullptr;
  lv_obj_t* icon_label = nullptr;
  lv_obj_t* temp_label = nullptr;
  lv_obj_t* temp_high_label = nullptr;
  lv_obj_t* temp_high_unit_label = nullptr;
  lv_obj_t* temp_low_label = nullptr;
  lv_obj_t* temp_low_unit_label = nullptr;
};

static constexpr uint8_t WEATHER_FORECAST_MAX = 8;
#if defined(DEVICE_LAYOUT_1024X600)
static constexpr lv_coord_t WEATHER_FORECAST_COL_W = 125;
#elif defined(DEVICE_LAYOUT_480X480)
static constexpr lv_coord_t WEATHER_FORECAST_COL_W = 100;
#else
static constexpr lv_coord_t WEATHER_FORECAST_COL_W = 150;
#endif

// The card's real size decides, not its slots, so every grid gets what
// fits: as many forecast days as their texts leave even room around
// (weather_forecast_count()), the forecast row from a two-row card of the
// classic grid (device.h; 2026-10-08: the fixed heights of the former grid
// hid the row once the classic grid kept a grid gap to the screen edge), the
// condition beside the temperature whenever it fits.
static constexpr lv_coord_t WEATHER_FORECAST_MIN_H = 2 * Device::kGridCellH + Device::kGridGap;
#if defined(DEVICE_LAYOUT_1024X600)
static constexpr lv_coord_t WEATHER_CONDITION_NARROW_W = 250;
#elif defined(DEVICE_LAYOUT_480X480)
static constexpr lv_coord_t WEATHER_CONDITION_NARROW_W = 176;
#else
static constexpr lv_coord_t WEATHER_CONDITION_NARROW_W = 264;
#endif
inline uint8_t weather_whole_cells(float span) {
  return span < 1.0f ? 1 : static_cast<uint8_t>(span);
}
// The condition joins the temperature whenever it fits (the state update
// checks the real room); below two day widths only when it fits completely.
inline bool weather_shows_condition(float) { return true; }
inline bool weather_condition_narrow(lv_coord_t card_w) { return card_w < WEATHER_CONDITION_NARROW_W; }
inline bool weather_shows_forecast(lv_coord_t card_h) { return card_h >= WEATHER_FORECAST_MIN_H; }

// The widest text of a forecast day in a language, and in the panel's
// (renderer.cpp).
lv_coord_t weather_forecast_text_width(const char* language);
lv_coord_t weather_forecast_text_width();
// The room between two days and at the card's edges: 85 % of a day's text,
// the look of five days on the 4B's 3 x 2 card (user 2026-10-07).
inline lv_coord_t weather_forecast_gap(lv_coord_t text_w) { return text_w * 85 / 100; }
// As many days as fit with that room between and around them.
inline uint8_t weather_forecast_count(lv_coord_t card_w) {
  const lv_coord_t text_w = weather_forecast_text_width();
  const lv_coord_t gap = weather_forecast_gap(text_w);
  const lv_coord_t days = text_w + gap > 0 ? (card_w - gap) / (text_w + gap) : 1;
  return days < 1 ? 1 : days > WEATHER_FORECAST_MAX ? WEATHER_FORECAST_MAX : static_cast<uint8_t>(days);
}

struct WeatherTileWidgets {
  lv_obj_t* icon_label = nullptr;
  lv_obj_t* temp_label = nullptr;
  lv_obj_t* condition_label = nullptr;
  lv_obj_t* condition_sep_label = nullptr;
  lv_obj_t* location_label = nullptr;
  WeatherForecastWidgets forecast[WEATHER_FORECAST_MAX];
  uint32_t last_payload_hash = 0;
};
