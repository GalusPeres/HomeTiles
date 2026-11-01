#pragma once

// The maintainer's test builds measure every opening too (user 2026-10-14:
// the Weather popup still felt slow, the log showed no drawing time).
#if (defined(HOMETILES_ISSUE38_BETA) || defined(HOMETILES_TEST_BETA)) && !defined(HOMETILES_POPUP_TIMING)
#define HOMETILES_POPUP_TIMING
#endif

#if defined(HOMETILES_POPUP_TIMING)
#include <Arduino.h>
#include <lvgl.h>
#include <lvgl_private.h>
#include <stdlib.h>
#include "src/ui/popups/popup_shell.h"

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

// Where an opening's first frame spends its drawing time: the screen under
// the popup, its overlay, the overlay's children (frame, body, header) and
// every block of the body. LVGL draws one object after the other here
// (LV_OS_NONE, one software draw unit), so a block's time runs from its
// DRAW_MAIN_BEGIN to the next marked object's or to its band's flush.
constexpr int kMaxSegments = 32;
struct Segment {
  lv_obj_t* obj;
  char kind;  // 'u' under the popup, 'v' overlay, 'o' overlay child, 'B' body, 'b' body child
  uint32_t us;
};
inline Segment segments[kMaxSegments];
inline int segment_count = 0;
inline int open_segment = -1;
inline uint32_t open_since = 0;
inline int unmarked = 0;
inline void close_segment(uint32_t now) {
  if (open_segment >= 0) segments[open_segment].us += now - open_since;
  open_segment = -1;
}
inline void open_segment_at(int index, uint32_t now) {
  close_segment(now);
  open_segment = index;
  open_since = now;
}
inline void mark_event(lv_event_t* event) {
  open_segment_at(static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event))), micros());
}
inline void add_mark(lv_obj_t* obj, char kind) {
  if (segment_count >= kMaxSegments) {
    ++unmarked;
    return;
  }
  segments[segment_count] = {obj, kind, 0};
  lv_obj_add_event_cb(obj, mark_event, LV_EVENT_DRAW_MAIN_BEGIN,
                      reinterpret_cast<void*>(static_cast<intptr_t>(segment_count)));
  ++segment_count;
}
inline void mark_popup() {
  segment_count = unmarked = 0;
  open_segment = -1;
  lv_obj_t* body = popup_shell_body();
  lv_obj_t* overlay = body ? lv_obj_get_parent(body) : nullptr;
  if (!overlay) return;
  segments[segment_count++] = {nullptr, 'u', 0};
  add_mark(overlay, 'v');
  for (uint32_t i = 0; i < lv_obj_get_child_count(overlay); ++i) {
    lv_obj_t* child = lv_obj_get_child(overlay, static_cast<int32_t>(i));
    add_mark(child, child == body ? 'B' : 'o');
  }
  for (uint32_t i = 0; i < lv_obj_get_child_count(body); ++i) {
    add_mark(lv_obj_get_child(body, static_cast<int32_t>(i)), 'b');
  }
}
inline void unmark_popup() {
  for (int i = 0; i < segment_count; ++i) {
    if (segments[i].obj) lv_obj_remove_event_cb(segments[i].obj, mark_event);
  }
  segment_count = 0;
  open_segment = -1;
}
inline uint32_t descendants(lv_obj_t* obj) {
  uint32_t count = 0;
  for (uint32_t i = 0; i < lv_obj_get_child_count(obj); ++i) {
    count += 1 + descendants(lv_obj_get_child(obj, static_cast<int32_t>(i)));
  }
  return count;
}
// One line: each part's time in ms (blocks under 0.5 ms left out), its size
// and how many objects it holds.
inline void report_segments() {
  // In the heap (PSRAM on the S3), not in internal RAM (psram_static.h).
  constexpr size_t kLineBytes = 1024;
  static char* const line = static_cast<char*>(malloc(kLineBytes));
  if (!line) return;
  size_t used = snprintf(line, kLineBytes, "[PopupDraw] under=%lu.%lu",
                         static_cast<unsigned long>(segments[0].us / 1000),
                         static_cast<unsigned long>(segments[0].us / 100 % 10));
  int overlay_child = 0, body_child = 0;
  for (int i = 1; i < segment_count && used < kLineBytes; ++i) {
    const Segment& s = segments[i];
    char name[12];
    if (s.kind == 'v') snprintf(name, sizeof(name), "overlay");
    else if (s.kind == 'B') snprintf(name, sizeof(name), "body");
    else if (s.kind == 'o') snprintf(name, sizeof(name), "o%d", overlay_child++);
    else snprintf(name, sizeof(name), "b%d", body_child++);
    if (s.us < 500) continue;
    used += snprintf(line + used, kLineBytes - used, " | %s %ldx%ld n=%lu %lu.%lu", name,
                     static_cast<long>(lv_obj_get_width(s.obj)), static_cast<long>(lv_obj_get_height(s.obj)),
                     static_cast<unsigned long>(descendants(s.obj)), static_cast<unsigned long>(s.us / 1000),
                     static_cast<unsigned long>(s.us / 100 % 10));
  }
  if (unmarked && used < kLineBytes) snprintf(line + used, kLineBytes - used, " | %d unmarked", unmarked);
  Serial.printf("%s\n", line);
}

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
        source, state, static_cast<lv_state_t>(state | LV_STATE_PRESSED), nullptr) ==
        LV_STYLE_STATE_CMP_DIFF_LAYOUT;
  } else if (sample.active && code == LV_EVENT_RELEASED) {
    sample.released = now;
  } else if (sample.active &&
             (code == LV_EVENT_SHORT_CLICKED || code == LV_EVENT_LONG_PRESSED)) {
    sample.clicked = now;
    sample.click_pending = true;
  }
}

// Scrolling (user 2026-10-19: the Media library stuttered on the V2, the
// Settings dropdown did not): one line per scroll of any object, so both are
// measured alike. While the touch drags or throws an object (the input's
// scroll object): the frames that drew, their time and the longest wait
// between two of them, the flushed pixels and their flush time.
struct ScrollSample {
  lv_obj_t* obj = nullptr;
  int32_t width = 0, height = 0;
  uint32_t started = 0, frame_started = 0, flush_started = 0, last_frame = 0;
  uint32_t frames = 0, frame_us = 0, frame_max_us = 0, gap_max_us = 0;
  uint32_t flush_us = 0, flushes = 0, pixels = 0, frame_flushes = 0;
  bool in_frame = false;
};
inline ScrollSample scroll;
inline lv_indev_t* scroll_input = nullptr;

inline void report_scroll(uint32_t now) {
  if (scroll.frames) {
    Serial.printf(
        "[ScrollPerf] obj=%ldx%ld ms=%lu frames=%lu frame_avg_us=%lu frame_max_us=%lu gap_max_us=%lu "
        "flush_avg_us=%lu flushes_avg=%lu px_avg=%lu\n",
        static_cast<long>(scroll.width), static_cast<long>(scroll.height),
        static_cast<unsigned long>((now - scroll.started) / 1000), static_cast<unsigned long>(scroll.frames),
        static_cast<unsigned long>(scroll.frame_us / scroll.frames), static_cast<unsigned long>(scroll.frame_max_us),
        static_cast<unsigned long>(scroll.gap_max_us), static_cast<unsigned long>(scroll.flush_us / scroll.frames),
        static_cast<unsigned long>(scroll.flushes / scroll.frames), static_cast<unsigned long>(scroll.pixels / scroll.frames));
  }
  scroll = {};
}

inline void scroll_display_event(lv_event_code_t code, lv_event_t* event, uint32_t now) {
  if (code == LV_EVENT_REFR_START) {
    lv_obj_t* obj = scroll_input ? lv_indev_get_scroll_obj(scroll_input) : nullptr;
    if (obj != scroll.obj) {
      if (scroll.obj) report_scroll(now);
      if (obj) {
        scroll.obj = obj;
        scroll.width = lv_obj_get_width(obj);
        scroll.height = lv_obj_get_height(obj);
        scroll.started = now;
      }
    }
    if (!scroll.obj) return;
    scroll.in_frame = true;
    scroll.frame_started = now;
    scroll.frame_flushes = 0;
  } else if (!scroll.in_frame) {
    return;
  } else if (code == LV_EVENT_FLUSH_START) {
    scroll.flush_started = now;
    ++scroll.frame_flushes;
    const auto* area = static_cast<const lv_area_t*>(lv_event_get_param(event));
    if (area) scroll.pixels += lv_area_get_size(area);
  } else if (code == LV_EVENT_FLUSH_FINISH) {
    scroll.flush_us += now - scroll.flush_started;
  } else if (code == LV_EVENT_REFR_READY) {
    scroll.in_frame = false;
    if (!scroll.frame_flushes) return;  // a timer pass that drew nothing
    const uint32_t frame_us = now - scroll.frame_started;
    ++scroll.frames;
    scroll.frame_us += frame_us;
    scroll.flushes += scroll.frame_flushes;
    if (frame_us > scroll.frame_max_us) scroll.frame_max_us = frame_us;
    if (scroll.last_frame && scroll.frame_started - scroll.last_frame > scroll.gap_max_us) {
      scroll.gap_max_us = scroll.frame_started - scroll.last_frame;
    }
    scroll.last_frame = scroll.frame_started;
  }
}

inline void display_event(lv_event_t* event) {
  const auto code = lv_event_get_code(event);
  const uint32_t now = micros();
  scroll_display_event(code, event, now);
  if (!sample.active) return;
  if (code == LV_EVENT_REFR_START) {
    sample.frame_started = now;
    sample.flush_us = sample.flushes = sample.pixels = 0;
    sample.in_frame = true;
    if (sample.click_pending) mark_popup();
  } else if (sample.in_frame && code == LV_EVENT_RENDER_START) {
    if (segment_count) open_segment_at(0, now);
  } else if (sample.in_frame && code == LV_EVENT_FLUSH_START) {
    if (segment_count) close_segment(now);
    sample.flush_started = now;
    ++sample.flushes;
    const auto* area = static_cast<const lv_area_t*>(lv_event_get_param(event));
    if (area) sample.pixels += lv_area_get_size(area);
  } else if (sample.in_frame && code == LV_EVENT_FLUSH_FINISH) {
    sample.flush_us += now - sample.flush_started;
    if (segment_count) open_segment_at(0, now);
  } else if (sample.in_frame && code == LV_EVENT_REFR_READY) {
    sample.in_frame = false;
    // The open "under" segment after the last flush drew nothing.
    open_segment = -1;
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
    if (!sample.flushes) {
      unmark_popup();
      return;
    }
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
    if (segment_count) {
      report_segments();
      unmark_popup();
    }
  }
}

inline void attach(lv_display_t* display, lv_indev_t* input) {
  scroll_input = input;
  lv_display_add_event_cb(display, display_event, LV_EVENT_ALL, nullptr);
  lv_indev_add_event_cb(input, input_event, LV_EVENT_ALL, nullptr);
}
}  // namespace popup_timing
#endif
