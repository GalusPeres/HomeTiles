#pragma once

#if defined(HOMETILES_POPUP_TIMING)
#include <Arduino.h>
#include <lvgl.h>
#include <lvgl_private.h>

// Diagnostic builds only: one current touch, no history, buffers or timers.
// Report after the first refresh following a click, outside the measured work.
namespace popup_timing {
struct Sample {
  uint32_t pressed = 0, released = 0, clicked = 0;
  uint32_t frame_started = 0, flush_started = 0, flush_us = 0;
  uint32_t press_frame_us = 0, press_frame_max_us = 0, press_flush_us = 0;
  uint32_t pixels = 0, flushes = 0, press_frames = 0;
  int32_t width = 0, height = 0;
  bool active = false, click_pending = false, in_frame = false;
  bool press_layout = false;
};
inline Sample sample;

inline void input_event(lv_event_t* event) {
  const auto code = lv_event_get_code(event);
  const uint32_t now = micros();
  if (code == LV_EVENT_PRESSED) {
    sample = {};
    auto* source = static_cast<lv_obj_t*>(lv_event_get_param(event));
    if (!source) return;
    sample.active = true;
    sample.pressed = now;
    sample.width = lv_obj_get_width(source);
    sample.height = lv_obj_get_height(source);
    const auto state = lv_obj_get_state(source);
    sample.press_layout = lv_obj_style_state_compare(
        source, state, static_cast<lv_state_t>(state | LV_STATE_PRESSED)) == LV_STYLE_STATE_CMP_DIFF_LAYOUT;
  } else if (sample.active && code == LV_EVENT_RELEASED) {
    sample.released = now;
  } else if (sample.active &&
             (code == LV_EVENT_SHORT_CLICKED || code == LV_EVENT_LONG_PRESSED)) {
    sample.clicked = now;
    sample.click_pending = true;
  }
}

inline void display_event(lv_event_t* event) {
  if (!sample.active) return;
  const auto code = lv_event_get_code(event);
  const uint32_t now = micros();
  if (code == LV_EVENT_REFR_START) {
    sample.frame_started = now;
    sample.flush_us = sample.flushes = sample.pixels = 0;
    sample.in_frame = true;
  } else if (sample.in_frame && code == LV_EVENT_FLUSH_START) {
    sample.flush_started = now;
    ++sample.flushes;
    const auto* area = static_cast<const lv_area_t*>(lv_event_get_param(event));
    if (area) sample.pixels += lv_area_get_size(area);
  } else if (sample.in_frame && code == LV_EVENT_FLUSH_FINISH) {
    sample.flush_us += now - sample.flush_started;
  } else if (sample.in_frame && code == LV_EVENT_REFR_READY) {
    sample.in_frame = false;
    const uint32_t frame_us = now - sample.frame_started;
    if (!sample.click_pending) {
      if (sample.flushes) {
        ++sample.press_frames;
        sample.press_frame_us += frame_us;
        sample.press_flush_us += sample.flush_us;
        if (frame_us > sample.press_frame_max_us) sample.press_frame_max_us = frame_us;
      }
      return;
    }
    // Ignore timer passes that did not draw; the next dirty frame is the result.
    if (!sample.flushes) return;
    sample.active = false;
    Serial.printf(
        "[PopupPerf] source=%ldx%ld hold_us=%lu release_to_click_us=%lu "
        "click_to_frame_us=%lu frame_us=%lu flush_us=%lu flushes=%lu pixels=%lu "
        "press_frames=%lu press_frame_us=%lu press_flush_us=%lu press_max_frame_us=%lu press_layout=%u\n",
        static_cast<long>(sample.width), static_cast<long>(sample.height),
        static_cast<unsigned long>((sample.released ? sample.released : sample.clicked) - sample.pressed),
        static_cast<unsigned long>(sample.released ? sample.clicked - sample.released : 0),
        static_cast<unsigned long>(sample.frame_started - sample.clicked),
        static_cast<unsigned long>(frame_us), static_cast<unsigned long>(sample.flush_us),
        static_cast<unsigned long>(sample.flushes), static_cast<unsigned long>(sample.pixels),
        static_cast<unsigned long>(sample.press_frames),
        static_cast<unsigned long>(sample.press_frame_us),
        static_cast<unsigned long>(sample.press_flush_us),
        static_cast<unsigned long>(sample.press_frame_max_us), sample.press_layout ? 1u : 0u);
  }
}

inline void attach(lv_display_t* display, lv_indev_t* input) {
  lv_display_add_event_cb(display, display_event, LV_EVENT_ALL, nullptr);
  lv_indev_add_event_cb(input, input_event, LV_EVENT_ALL, nullptr);
}
}  // namespace popup_timing
#endif
