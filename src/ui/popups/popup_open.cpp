#include "src/ui/popups/popup_open.h"
#if defined(ARDUINO)
#include <Arduino.h>
#endif
#include "src/ui/popups/popup_first_frame.h"
#include "src/ui/popups/popup_body.h"

namespace {
#if defined(ARDUINO)
// TEMPORARY diagnostic (b85): times each popup opening to find why popups
// open slowly after the wide System popup on P4. One line per opening; the
// display callback is attached only while an opening is measured.
struct OpenPerf {
  lv_display_t* display = nullptr;
  uint32_t begin_ms = 0, refr_start_ms = 0, shell_frame_ms = 0, wait_ms = 0;
  uint32_t apply_ms = 0, content_frame_ms = 0;
  // The 30 refreshes after the content frame (e.g. while a slider moves).
  uint32_t later_sum_ms = 0, later_max_ms = 0;
  uint8_t later_count = 0;
  bool logged = false;
  uint8_t frames = 0;
  bool keep_visible = false, applied = false;
  int32_t width = 0, height = 0;
} perf;

void perf_log() {
  Serial.printf("[PopupPerf] open %ldx%ld keep=%d shell_frame=%lu ms wait=%lu ms "
                "apply=%lu ms content_frame=%lu ms total=%lu ms\n",
                static_cast<long>(perf.width), static_cast<long>(perf.height),
                perf.keep_visible ? 1 : 0, static_cast<unsigned long>(perf.shell_frame_ms),
                static_cast<unsigned long>(perf.wait_ms), static_cast<unsigned long>(perf.apply_ms),
                static_cast<unsigned long>(perf.content_frame_ms),
                static_cast<unsigned long>(millis() - perf.begin_ms));
}

void perf_refreshed(lv_event_t* event) {
  const lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_REFR_START) {
    perf.refr_start_ms = millis();
    return;
  }
  if (code != LV_EVENT_REFR_READY) return;
  const uint32_t duration = millis() - perf.refr_start_ms;
  if (!perf.applied) {
    if (perf.frames++ == 0) perf.shell_frame_ms = duration;
    return;
  }
  if (!perf.logged) {
    perf.content_frame_ms = duration;
    perf.logged = true;
    perf_log();
    return;
  }
  perf.later_sum_ms += duration;
  if (duration > perf.later_max_ms) perf.later_max_ms = duration;
  if (++perf.later_count < 30) return;
  Serial.printf("[PopupPerf] next 30 frames avg=%lu ms max=%lu ms\n",
                static_cast<unsigned long>(perf.later_sum_ms / 30),
                static_cast<unsigned long>(perf.later_max_ms));
  lv_display_remove_event_cb_with_user_data(perf.display, perf_refreshed, nullptr);
  perf.display = nullptr;
}

void perf_begin(lv_obj_t* card, bool keep_visible) {
  if (perf.display) lv_display_remove_event_cb_with_user_data(perf.display, perf_refreshed, nullptr);
  perf = {};
  perf.begin_ms = millis();
  perf.keep_visible = keep_visible;
  // Style sizes: pixels for tile popups, LV_PCT codes for full-size Settings.
  perf.width = lv_obj_get_style_width(card, LV_PART_MAIN);
  perf.height = lv_obj_get_style_height(card, LV_PART_MAIN);
  perf.display = lv_display_get_default();
  if (perf.display) lv_display_add_event_cb(perf.display, perf_refreshed, LV_EVENT_ALL, nullptr);
}

uint32_t perf_now() { return millis(); }
#else
struct { uint32_t begin_ms, wait_ms, apply_ms; bool applied; } perf{};
void perf_begin(lv_obj_t*, bool) {}
uint32_t perf_now() { return 0; }
#endif

struct Opening {
  lv_obj_t* card = nullptr;
  void* payload = nullptr;
  void (*apply)(void*) = nullptr;
  void (*destroy)(void*) = nullptr;
  void (*simple_apply)() = nullptr;
  PopupFirstFrame frame;
  PopupBody body;
} opening;

void on_delete(lv_event_t*);

void clear(bool deleting = false) {
  opening.frame.cancel();
  if (opening.card && !deleting)
    lv_obj_remove_event_cb(opening.card, on_delete);
  if (deleting) opening.body.forget();
  else opening.body.restore();
  if (opening.destroy) opening.destroy(opening.payload);
  opening.card = nullptr;
  opening.payload = nullptr;
  opening.apply = nullptr;
  opening.destroy = nullptr;
  opening.simple_apply = nullptr;
}

void on_delete(lv_event_t*) { clear(true); }

void begin(lv_obj_t* card) {
  opening.card = card;
  lv_obj_add_event_cb(card, on_delete, LV_EVENT_DELETE, nullptr);
  lv_obj_invalidate(card);
  opening.frame.begin();
}
}

void popup_open_detail::schedule(lv_obj_t* card, lv_obj_t* title, lv_obj_t* icon,
                                  lv_obj_t* close, void* payload,
                                  void (*apply)(void*), void (*destroy)(void*), bool keep_visible) {
  clear();
  perf_begin(card, keep_visible);
  if (!keep_visible) opening.body.hide(card, title, icon, close);
  opening.payload = payload;
  opening.apply = apply;
  opening.destroy = destroy;
  begin(card);
}

void defer_popup_content(lv_obj_t* card, void (*apply)()) {
  clear();
  perf_begin(card, false);
  opening.simple_apply = apply;
  begin(card);
}

bool popup_open_pending(lv_obj_t* card) { return card && opening.card == card; }

void cancel_popup_open(lv_obj_t* card) {
  if (popup_open_pending(card)) clear();
}

void process_popup_open() {
  if (!opening.card || opening.frame.pending()) return;
  if (lv_obj_has_flag(opening.card, LV_OBJ_FLAG_HIDDEN)) { clear(); return; }
  // Detach before invoking a callback: Sensor may request a controls frame,
  // and a callback may schedule another opening without losing its state.
  auto* payload = opening.payload;
  auto apply = opening.apply;
  auto destroy = opening.destroy;
  auto simple_apply = opening.simple_apply;
  opening.payload = nullptr;
  opening.destroy = nullptr;
  clear();
  perf.wait_ms = perf_now() - perf.begin_ms;
  const uint32_t apply_start = perf_now();
  if (apply) apply(payload);
  else if (simple_apply) simple_apply();
  if (destroy) destroy(payload);
  perf.apply_ms = perf_now() - apply_start;
  perf.applied = true;
}
