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
    lv_display_t* display = lv_display_get_default();
    lv_timer_t* refresh = display ? lv_display_get_refr_timer(display) : nullptr;
    // LVGL pauses its refresh timer after every frame and resumes it on the
    // next invalidation or layout change; lv_timer_ready() does not resume
    // it. A paused timer therefore means nothing waits to be drawn: the
    // controls are on screen and no REFR_READY follows. Waiting anyway held
    // every gated queue (tile updates, Energy answers, popup history), and
    // those were the only changes left to draw: the tiles stood still
    // overnight until a touch drew something (user 2026-10-31). Display sleep
    // pauses the timer too; its wake draws the whole screen.
    if (!refresh || lv_timer_get_paused(refresh)) return;
    display_ = display;
    ++pending_count_;
    lv_display_add_event_cb(display_, refreshed, LV_EVENT_ALL, this);
    lv_timer_ready(refresh);
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
