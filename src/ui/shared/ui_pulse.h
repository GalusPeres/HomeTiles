#pragma once

#include <lvgl.h>

// Home Assistant's state-control pulse (opacity 1 -> 0 -> 1 in 1 s, eased in
// and out) for an icon while a command runs or a device needs attention.
// Every pulse reads one shared clock instead of counting from its own start,
// so the popup header icon, the popup's control symbol and the tile breathe
// in step, however late each one started or how often the popup rebuilt its
// control (user 02.10.: the middle and the top left pulsed apart).
namespace ui_pulse {

constexpr uint32_t kHalfMs = 500;

// The opacity every pulse shows right now.
inline lv_opa_t opa_now() {
  const uint32_t t = lv_tick_get() % (2 * kHalfMs);
  const int32_t x = lv_map(static_cast<int32_t>(t < kHalfMs ? t : 2 * kHalfMs - t), 0,
                           static_cast<int32_t>(kHalfMs), 0, LV_BEZIER_VAL_MAX);
  const int32_t step = lv_cubic_bezier(x, LV_BEZIER_VAL_FLOAT(0.42), LV_BEZIER_VAL_FLOAT(0),
                                       LV_BEZIER_VAL_FLOAT(0.58), LV_BEZIER_VAL_FLOAT(1));
  return static_cast<lv_opa_t>(LV_OPA_COVER - ((LV_OPA_COVER * step) >> LV_BEZIER_VAL_SHIFT));
}

// Calls `exec` once now and then every frame until lv_anim_delete(var,
// exec); `exec` shows opa_now() (the animation's own value only keeps the
// frames coming).
inline void start(void* var, lv_anim_exec_xcb_t exec) {
  lv_anim_t anim;
  lv_anim_init(&anim);
  lv_anim_set_var(&anim, var);
  lv_anim_set_exec_cb(&anim, exec);
  lv_anim_set_values(&anim, 0, 2 * kHalfMs);
  lv_anim_set_duration(&anim, 2 * kHalfMs);
  lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
  lv_anim_start(&anim);
  exec(var, 0);
}

}  // namespace ui_pulse
