#pragma once

#include <lvgl.h>

// Gate optional content until the normal LVGL refresh has drawn the controls.
// No nested refresh, timer, worker, or allocated frame buffer is needed.
class PopupFirstFrame {
 public:
  PopupFirstFrame() = default;
  PopupFirstFrame(const PopupFirstFrame&) = delete;
  PopupFirstFrame& operator=(const PopupFirstFrame&) = delete;
  ~PopupFirstFrame() { cancel(); }

  void begin() {
    cancel();
    display_ = lv_display_get_default();
    if (display_) {
      ++pending_count_;
      lv_display_add_event_cb(display_, refreshed, LV_EVENT_ALL, this);
      lv_timer_ready(lv_display_get_refr_timer(display_));
    }
  }

  void cancel() {
    if (display_) {
      lv_display_remove_event_cb_with_user_data(display_, refreshed, this);
      display_ = nullptr;
      --pending_count_;
    }
  }

  bool pending() const { return display_ != nullptr; }
  static bool any_pending() { return pending_count_ != 0; }

 private:
  static void refreshed(lv_event_t* event) {
    if (lv_event_get_code(event) == LV_EVENT_REFR_READY ||
        lv_event_get_code(event) == LV_EVENT_DELETE)
      static_cast<PopupFirstFrame*>(lv_event_get_user_data(event))->cancel();
  }
  lv_display_t* display_ = nullptr;
  inline static unsigned pending_count_ = 0;
};
