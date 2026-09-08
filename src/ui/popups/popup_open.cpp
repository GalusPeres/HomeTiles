#include "src/ui/popups/popup_open.h"
#include "src/ui/popups/popup_first_frame.h"
#include "src/ui/popups/popup_body.h"

namespace {
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
  if (!keep_visible) opening.body.hide(card, title, icon, close);
  opening.payload = payload;
  opening.apply = apply;
  opening.destroy = destroy;
  begin(card);
}

void defer_popup_content(lv_obj_t* card, void (*apply)()) {
  clear();
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
  if (apply) apply(payload);
  else if (simple_apply) simple_apply();
  if (destroy) destroy(payload);
}
