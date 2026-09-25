#pragma once

#include <lvgl.h>
#include <initializer_list>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

// Finger readout for popup graphs (Sensor, Number, Select, Binary Sensor and
// Energy). The graph's touch surface keeps the press, so dragging never
// scrolls, swipes or clicks anything else. Pointer moves are only stored; the
// owner applies them once per display refresh (LV_EVENT_REFR_START). Release,
// press loss and an input reset end the readout at once. Nothing is allocated
// while dragging.
class PopupGraphScrub {
 public:
  using Apply = void (*)(void* owner, lv_obj_t* target, const lv_point_t& point);
  using End = void (*)(void* owner);

  PopupGraphScrub() = default;
  PopupGraphScrub(const PopupGraphScrub&) = delete;
  PopupGraphScrub& operator=(const PopupGraphScrub&) = delete;
  ~PopupGraphScrub() { stop_refresh(); }

  void init(void* owner, Apply apply, End end) {
    owner_ = owner;
    apply_ = apply;
    end_ = end;
  }

  // Make `target` a graph touch surface. Its input events are registered once.
  void attach(lv_obj_t* target) {
    if (!target) return;
    for (uint32_t i = 0; i < lv_obj_get_event_count(target); ++i) {
      if (lv_event_dsc_get_cb(lv_obj_get_event_dsc(target, i)) == input) return;
    }
    lv_obj_add_flag(target, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE |
                                                       LV_OBJ_FLAG_PRESS_LOCK));
    lv_obj_remove_flag(target, static_cast<lv_obj_flag_t>(
                                   LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_CHAIN |
                                   LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_EVENT_BUBBLE |
                                   LV_OBJ_FLAG_CLICK_FOCUSABLE |
                                   LV_OBJ_FLAG_SCROLL_ON_FOCUS));
    // No DELETE registration: the owner cancels before it releases this object.
    for (lv_event_code_t code : {LV_EVENT_PRESSED, LV_EVENT_PRESSING, LV_EVENT_RELEASED,
                                 LV_EVENT_PRESS_LOST, LV_EVENT_INDEV_RESET}) {
      lv_obj_add_event_cb(target, input, code, this);
    }
  }

  bool active() const { return active_; }

  // Re-apply the current finger position with the next refresh, for example
  // after the data under the finger changed.
  void request_apply() {
    if (active_ && target_) schedule();
  }

  // End the readout now: popup hidden, content replaced or owner teardown.
  void cancel() {
    const bool was_active = active_;
    active_ = false;
    pending_ = false;
    target_ = nullptr;
    stop_refresh();
    if (was_active && end_) end_(owner_);
  }

 private:
  static void input(lv_event_t* event) {
    auto* self = static_cast<PopupGraphScrub*>(lv_event_get_user_data(event));
    auto* target = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
    if (!self || !target) return;
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) {
      self->cancel();
      self->active_ = true;
      self->target_ = target;
    } else if (code != LV_EVENT_PRESSING) {
      if (self->target_ == target) self->cancel();
      return;
    }
    if (!self->active_ || self->target_ != target) return;
    lv_indev_t* indev = lv_indev_active();
    if (!indev) return;
    lv_point_t point;
    lv_indev_get_point(indev, &point);
    // A resting finger keeps sending PRESSING; only a new position needs work.
    if (code == LV_EVENT_PRESSING && point.x == self->point_.x && point.y == self->point_.y) return;
    self->point_ = point;
    self->schedule();
  }

  void schedule() {
    if (!display_) {
      display_ = lv_obj_get_display(target_);
      if (!display_) return;
      lv_display_add_event_cb(display_, refresh, LV_EVENT_REFR_START, this);
      lv_display_add_event_cb(display_, refresh, LV_EVENT_DELETE, this);
    }
    pending_ = true;
    // An idle refresh timer is paused. Resuming keeps its period, so the
    // stored position is applied with the next regular frame, not earlier.
    if (lv_timer_t* timer = lv_display_get_refr_timer(display_)) lv_timer_resume(timer);
  }

  static void refresh(lv_event_t* event) {
    auto* self = static_cast<PopupGraphScrub*>(lv_event_get_user_data(event));
    if (!self) return;
    if (lv_event_get_code(event) == LV_EVENT_DELETE) {
      self->display_ = nullptr;
      return;
    }
    if (!self->pending_ || !self->active_ || !self->target_) return;
    self->pending_ = false;
    if (self->apply_) self->apply_(self->owner_, self->target_, self->point_);
  }

  void stop_refresh() {
    if (!display_) return;
    lv_display_remove_event_cb_with_user_data(display_, refresh, this);
    display_ = nullptr;
  }

  void* owner_ = nullptr;
  Apply apply_ = nullptr;
  End end_ = nullptr;
  lv_obj_t* target_ = nullptr;
  lv_display_t* display_ = nullptr;
  lv_point_t point_{};
  bool active_ = false;
  bool pending_ = false;
};

namespace popup_graph_readout {

// " – " (U+2013); every UI font includes the General Punctuation block.
constexpr const char* kRangeSeparator = " \xE2\x80\x93 ";

// One readout line in the old value row: fixed size and static text, so a
// change neither allocates nor triggers a layout pass.
inline lv_obj_t* create_band_label(lv_obj_t* band, const lv_font_t* font, const char* text) {
  lv_obj_t* label = lv_label_create(band);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, lv_color_white(), 0);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
  lv_obj_set_size(label, LV_PCT(100), lv_font_get_line_height(font));
  lv_obj_remove_flag(label, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE |
                                                       LV_OBJ_FLAG_SCROLLABLE));
  lv_label_set_text_static(label, text);
  lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
  return label;
}

// Center the small time line and the large value line as one block.
inline void align_band_labels(lv_obj_t* time_label, lv_obj_t* value_label) {
  if (!time_label || !value_label) return;
  const int32_t time_height =
      lv_font_get_line_height(lv_obj_get_style_text_font(time_label, LV_PART_MAIN));
  const int32_t value_height =
      lv_font_get_line_height(lv_obj_get_style_text_font(value_label, LV_PART_MAIN));
  lv_obj_align(time_label, LV_ALIGN_CENTER, 0, -value_height / 2);
  lv_obj_align(value_label, LV_ALIGN_CENTER, 0, time_height / 2);
}

inline void show_band_labels(lv_obj_t* time_label, lv_obj_t* value_label, bool visible) {
  for (lv_obj_t* label : {time_label, value_label}) {
    if (!label || lv_obj_has_flag(label, LV_OBJ_FLAG_HIDDEN) == !visible) continue;
    if (visible) lv_obj_remove_flag(label, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
  }
}

inline size_t append(char* out, size_t size, size_t used, const char* text) {
  if (!out || !size || used >= size || !text) return used;
  const int written = snprintf(out + used, size - used, "%s", text);
  if (written < 0) return used;
  const size_t next = used + static_cast<size_t>(written);
  return next < size ? next : size - 1;
}

// Local clock time in the configured 24/12-hour format, optionally prefixed
// by a weekday: "14:05", "2:05 PM", "Mon 14:05".
inline size_t append_clock(char* out, size_t size, size_t used, const struct tm& time,
                           bool twelve_hour, const char* day) {
  if (day && *day) {
    used = append(out, size, used, day);
    used = append(out, size, used, " ");
  }
  char clock[16];
  if (twelve_hour) {
    int hour = time.tm_hour % 12;
    if (hour == 0) hour = 12;
    snprintf(clock, sizeof(clock), "%d:%02d %s", hour, time.tm_min,
             time.tm_hour < 12 ? "AM" : "PM");
  } else {
    snprintf(clock, sizeof(clock), "%02d:%02d", time.tm_hour, time.tm_min);
  }
  return append(out, size, used, clock);
}

// Whole hour of an hourly bar: "11:00", or "11 AM" with the 12-hour clock.
inline size_t append_hour(char* out, size_t size, size_t used, int hour, bool twelve_hour) {
  hour %= 24;
  if (hour < 0) hour += 24;
  char text[12];
  if (twelve_hour) {
    int hour12 = hour % 12;
    if (hour12 == 0) hour12 = 12;
    snprintf(text, sizeof(text), "%d %s", hour12, hour < 12 ? "AM" : "PM");
  } else {
    snprintf(text, sizeof(text), "%02d:00", hour);
  }
  return append(out, size, used, text);
}

// Same result as i18n::format_number() plus " unit", written into a buffer.
inline void format_number(char* out, size_t size, float value, uint8_t digits,
                          char decimal_separator, const char* unit) {
  if (!out || !size) return;
  out[0] = '\0';
  if (!isfinite(value)) {
    append(out, size, 0, "--");
    return;
  }
  const int written = snprintf(out, size, "%.*f", digits > 6 ? 6 : digits,
                               static_cast<double>(value));
  if (written < 0) {
    out[0] = '\0';
    return;
  }
  if (decimal_separator == ',') {
    for (char* p = out; *p; ++p) {
      if (*p == '.') *p = ',';
    }
  }
  if (unit && *unit) {
    size_t used = strlen(out);
    used = append(out, size, used, " ");
    append(out, size, used, unit);
  }
}

}  // namespace popup_graph_readout
