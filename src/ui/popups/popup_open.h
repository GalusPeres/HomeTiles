#pragma once

#include <lvgl.h>
#include <esp_heap_caps.h>
#include <new>

// One pending opening for the whole UI. The loop executes content only after
// the regular display refresh has presented the resident card and header.
void defer_popup_content(lv_obj_t* card, void (*apply)());
void process_popup_open();
void cancel_popup_open(lv_obj_t* card);
bool popup_open_pending(lv_obj_t* card);

namespace popup_open_detail {
void schedule(lv_obj_t* card, lv_obj_t* title, lv_obj_t* icon, lv_obj_t* close,
              void* payload, void (*apply)(void*), void (*destroy)(void*), bool keep_visible);
}

template <typename Init>
bool defer_popup_body(lv_obj_t* card, lv_obj_t* title, lv_obj_t* icon,
                       lv_obj_t* close, const Init& init, void (*apply)(const Init&),
                       bool keep_visible = false) {
  struct Pending {
    Init init;
    void (*apply)(const Init&);
  };
  cancel_popup_open(card);
  // This transient binding is small; the existing widget/page caches and
  // internal draw band remain unchanged. Never take internal/DMA reserves.
  void* memory = heap_caps_malloc(sizeof(Pending), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!memory) return false;
  auto* pending = new (memory) Pending{init, apply};
  popup_open_detail::schedule(card, title, icon, close, pending,
      [](void* data) { auto* p = static_cast<Pending*>(data); p->apply(p->init); },
      [](void* data) { static_cast<Pending*>(data)->~Pending(); heap_caps_free(data); },
      keep_visible);
  return true;
}
