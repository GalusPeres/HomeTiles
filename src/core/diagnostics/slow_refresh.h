#pragma once

#include <Arduino.h>
#include <lvgl.h>

// A display refresh of 250 ms or more is logged with the screen region it
// drew (the bounding box of the flushed areas) and its flush count, so a slow
// redraw names the tile or overlay behind it. The V2 drew for about 300 ms
// once a minute on Home with no other log line (b304). Logging only; one
// sample, no buffers or history.
namespace slow_refresh {

struct Sample {
  uint32_t started_ms = 0;
  uint32_t flushes = 0;
  uint32_t pixels = 0;
  lv_area_t bounds{};
  bool in_frame = false;
};
inline Sample sample;
inline constexpr uint32_t kSlowRefreshMs = 250;

inline void display_event(lv_event_t* event) {
  const lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_REFR_START) {
    sample = {};
    sample.started_ms = millis();
    sample.in_frame = true;
  } else if (sample.in_frame && code == LV_EVENT_FLUSH_START) {
    const auto* area = static_cast<const lv_area_t*>(lv_event_get_param(event));
    if (!area) return;
    if (sample.flushes == 0) {
      sample.bounds = *area;
    } else {
      sample.bounds.x1 = LV_MIN(sample.bounds.x1, area->x1);
      sample.bounds.y1 = LV_MIN(sample.bounds.y1, area->y1);
      sample.bounds.x2 = LV_MAX(sample.bounds.x2, area->x2);
      sample.bounds.y2 = LV_MAX(sample.bounds.y2, area->y2);
    }
    ++sample.flushes;
    sample.pixels += lv_area_get_size(area);
  } else if (sample.in_frame && code == LV_EVENT_REFR_READY) {
    sample.in_frame = false;
    const uint32_t took = millis() - sample.started_ms;
    if (took < kSlowRefreshMs) return;
    if (!sample.flushes) {
      Serial.printf("[SlowRefresh] %u ms, nothing flushed\n", static_cast<unsigned>(took));
      return;
    }
    Serial.printf("[SlowRefresh] %u ms, %u flushes, %u px, region x=%d y=%d w=%d h=%d\n",
                  static_cast<unsigned>(took), static_cast<unsigned>(sample.flushes),
                  static_cast<unsigned>(sample.pixels), static_cast<int>(sample.bounds.x1),
                  static_cast<int>(sample.bounds.y1),
                  static_cast<int>(lv_area_get_width(&sample.bounds)),
                  static_cast<int>(lv_area_get_height(&sample.bounds)));
  }
}

inline void attach(lv_display_t* display) {
  if (display) lv_display_add_event_cb(display, display_event, LV_EVENT_ALL, nullptr);
}

}  // namespace slow_refresh
