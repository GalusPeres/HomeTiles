#include "src/core/display/display_flush_gate.h"

#include <lvgl.h>
#include <lvgl_private.h>

namespace display_flush_gate {
namespace {

int32_t (*g_next_dispatch)(lv_draw_unit_t*, lv_layer_t*) = nullptr;

// While the copy runs the unit reports itself busy and asks LVGL to come back:
// LVGL goes on adding the band's tasks, and at the band's end
// (draw_buf_flush) it calls again until the copy finished. Without the
// request LVGL's wait for a request (no OS) would never return.
int32_t gated_dispatch(lv_draw_unit_t* unit, lv_layer_t* layer) {
  if (!ready()) {
    lv_draw_dispatch_request();
    return 0;
  }
  return g_next_dispatch(unit, layer);
}

}  // namespace

void install_dispatch() {
  if (g_dispatch_gated || !g_backend.begin || !g_backend.done || !g_backend.finish) return;
  lv_draw_unit_t* unit = LV_GLOBAL_DEFAULT()->draw_info.unit_head;
  // A second draw unit could write the buffer past this gate: copies stay
  // synchronous then.
  if (!unit || unit->next || !unit->dispatch_cb) return;
  g_next_dispatch = unit->dispatch_cb;
  unit->dispatch_cb = gated_dispatch;
  g_dispatch_gated = true;
}

}  // namespace display_flush_gate
