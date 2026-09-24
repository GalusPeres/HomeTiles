#pragma once

#include <stdint.h>
#include <lvgl.h>

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

// Map tile width (span_w) to number of forecast days shown
// Layout variants follow whole cells; a half cell only adds spacing.
inline uint8_t weather_whole_cells(float span) {
  return span < 1.0f ? 1 : static_cast<uint8_t>(span);
}
// From 1.5 cells the condition may join the temperature; the state update
// shows it only when it fits the real card width.
inline bool weather_shows_condition(float span_w) { return span_w > 1.0f; }
inline bool weather_shows_forecast(float span_h) { return weather_whole_cells(span_h) >= 2; }

inline uint8_t weather_forecast_count(float span) {
  const uint8_t span_w = span <= 0.0f ? 0 : static_cast<uint8_t>(span);
  switch (span_w) {
    case 1: return 1;
    case 2: return 2;
    case 3: return 4;
    case 4: return 5;
    case 5: return 6;
    case 6: return 8;
    default: return span_w >= 6 ? 8 : span_w;
  }
}

// Forecast days for a half-step card: whole spans keep their fixed count; a
// half step shows as many days as fit at the column density of the next whole
// span (next_w wide), so the count never jumps between neighbouring sizes.
inline uint8_t weather_forecast_count(float span, lv_coord_t card_w, lv_coord_t next_w) {
  const uint8_t whole = weather_forecast_count(span);
  if (span < 1.0f || span == static_cast<float>(static_cast<uint8_t>(span))) return whole;
  const uint8_t next = weather_forecast_count(span + 0.5f);
  if (next_w <= 0) return whole;
  const long fits = static_cast<long>(card_w) * next / next_w;
  if (fits <= whole) return whole;
  return static_cast<uint8_t>(fits < next ? fits : next);
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
