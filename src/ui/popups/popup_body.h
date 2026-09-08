#pragma once

#include <lvgl.h>
#include <stdint.h>

// Temporarily hide a resident popup's body without rebuilding its widgets or
// losing their individual visibility (for example Week versus Day graphs).
// The existing popup cards have fewer than 32 direct children.
class PopupBody {
 public:
  void hide(lv_obj_t* card, lv_obj_t* title, lv_obj_t* icon, lv_obj_t* close) {
    restore();
    if (!card) return;
    const uint32_t count = lv_obj_get_child_count(card);
    LV_ASSERT(count <= 32);
    card_ = card;
    for (uint32_t i = 0; i < count && i < 32; ++i) {
      auto* child = lv_obj_get_child(card, i);
      if (child == title || child == icon || child == close) continue;
      if (!lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN)) visible_ |= uint32_t(1) << i;
      lv_obj_add_flag(child, LV_OBJ_FLAG_HIDDEN);
    }
  }

  void restore() {
    if (card_) {
      const uint32_t count = lv_obj_get_child_count(card_);
      for (uint32_t i = 0; i < count && i < 32; ++i)
        if (visible_ & (uint32_t(1) << i))
          lv_obj_remove_flag(lv_obj_get_child(card_, i), LV_OBJ_FLAG_HIDDEN);
    }
    forget();
  }

  void forget() { card_ = nullptr; visible_ = 0; }

 private:
  lv_obj_t* card_ = nullptr;
  uint32_t visible_ = 0;
};
