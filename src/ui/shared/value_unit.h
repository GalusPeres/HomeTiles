#pragma once

#include <Arduino.h>
#include <lvgl.h>

#include "src/ui/shared/value_unit_rules.h"

// A number and its unit on the panel (value_unit_rules.h): the unit in its
// own smaller label right after the number, the number's label keeping room
// for it on its right, so a centred, left or right aligned value places the
// pair as one.
namespace value_unit {

// The unit's font for a number's font (unit_px over the panel's text fonts);
// the number's own font when it is not one of them.
const lv_font_t* unit_font(const lv_font_t* value_font);

// `value_label` shows `value`; `unit` (empty: none) goes into `unit_label`,
// created on first use as the value label's child (it goes with it). A pair
// wider than a fixed-width value label stays one text in it, as before.
// Unchanged texts change nothing.
void show(lv_obj_t* value_label, lv_obj_t*& unit_label, const String& value, const String& unit);

}  // namespace value_unit
