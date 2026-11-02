#pragma once

#include <lvgl.h>

// A frosted-glass tile card (screensaver frosted_backdrop.h): only its fill
// is see-through; its circles, controls and graphs keep the colors of an
// opaque card (user 2026-11-02, tile_icon_disc see_through and card_of).
// Marked by an event callback that never runs, the way tile_icon_disc's
// follow_card_states finds its own.
namespace tile_glass {

inline void marker(lv_event_t*) {}

inline bool is_glass(lv_obj_t* card) {
  if (!card) return false;
  const uint32_t count = lv_obj_get_event_count(card);
  for (uint32_t i = 0; i < count; ++i) {
    if (lv_event_dsc_get_cb(lv_obj_get_event_dsc(card, i)) == marker) return true;
  }
  return false;
}

inline void mark(lv_obj_t* card) {
  if (card && !is_glass(card)) lv_obj_add_event_cb(card, marker, LV_EVENT_LAST, nullptr);
}

}  // namespace tile_glass
