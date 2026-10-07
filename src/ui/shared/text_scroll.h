#pragma once

#include <lvgl.h>

// Text that does not fit scrolls back and forth (LVGL's scroll long mode): a
// 2 s pause at each end, then about 18 px/s, at least 0.3 s and at most 12 s
// per pass. The media popup's title and artist and the Settings pairing line
// move alike. A label whose text fits stays still.
namespace ui_text_scroll {

inline const lv_anim_t* animation() {
  static lv_anim_t anim;
  static bool initialized = false;
  if (!initialized) {
    lv_anim_init(&anim);
    lv_anim_set_delay(&anim, 2000);
    lv_anim_set_repeat_count(&anim, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_repeat_delay(&anim, 2000);
    lv_anim_set_reverse_delay(&anim, 2000);
    lv_anim_set_path_cb(&anim, lv_anim_path_linear);
    initialized = true;
  }
  return &anim;
}

inline void apply(lv_obj_t* label) {
  if (!label) return;
  lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL);
  lv_obj_set_style_anim(label, animation(), LV_PART_MAIN);
  lv_obj_set_style_anim_duration(label, lv_anim_speed_clamped(18, 300, 12000), LV_PART_MAIN);
}

}  // namespace ui_text_scroll
