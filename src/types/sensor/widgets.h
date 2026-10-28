#pragma once

#include <stdint.h>
#include <lvgl.h>

struct SensorTileWidgets {
  lv_obj_t* value_label = nullptr;
  // Icon for per-tile color rules; recolored only on state updates.
  lv_obj_t* icon_label = nullptr;
  lv_obj_t* unit_label = nullptr;
  lv_obj_t* gauge = nullptr;
  // The gauge's bounds with their decimals (gauge_range.h).
  float gauge_min = 0;
  float gauge_max = 100;
  lv_obj_t* chart = nullptr;
  lv_chart_series_t* series = nullptr;
  // A look below the header (sensor_look_view, display modes 3 and up).
  lv_obj_t* look = nullptr;
};
