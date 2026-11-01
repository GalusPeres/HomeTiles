#pragma once

#include <lvgl.h>

#include "src/ui/shared/value_unit_rules.h"

// A number and its unit on the panel (value_unit_rules.h): the unit in its
// own smaller label right after the number, the number's label keeping room
// for it on its right, so a centred, left or right aligned value places the
// pair as one. Texts are plain C strings; the templates take any string
// type with c_str() (Arduino's String, the host tests' strings).
namespace value_unit {

// The unit's font for a number's font (unit_px over the panel's text fonts);
// the number's own font when it is not one of them.
const lv_font_t* unit_font(const lv_font_t* value_font);

// `value_label` shows `value`; `unit` (empty: none) goes into `unit_label`,
// created on first use as the value label's child (it goes with it). A pair
// wider than a fixed-width value label stays one text in it, as before.
// Unchanged texts change nothing.
void show(lv_obj_t* value_label, lv_obj_t*& unit_label, const char* value, const char* unit);

// The two halves of show() for a value label whose text another renderer
// sets (the popup header's ellipsis, title_label.h): reserve() keeps the
// unit's room on the label's right (none and the unit hidden for an empty
// `unit`) and returns whether the unit shows; place() puts it after the
// label's text as drawn now.
bool reserve(lv_obj_t* value_label, lv_obj_t*& unit_label, const char* unit);
void place(lv_obj_t* value_label, lv_obj_t* unit_label);
// place() for a value label's tagged unit (after its width changed).
void place(lv_obj_t* value_label);

// The panel's text font sizes a unit takes, ascending (the Web Admin
// preview picks the same step, sensor web_scripts.cpp).
const uint8_t* unit_font_sizes(uint8_t& count);

// The unit a value label shows (its shown unit child), "" for none.
const char* unit_text(lv_obj_t* value_label);

// show() with the unit label found as the value label's tagged child, for
// labels whose owners keep no unit label.
void show(lv_obj_t* value_label, const char* value, const char* unit);

// A finished text: its unit after a number apart (split_unit, "21,5 °C",
// "Open · 58 %"), any other text as it is.
void show_text(lv_obj_t* value_label, const char* text);

inline const char* cstr(const char* text) { return text ? text : ""; }
template <typename T>
inline const char* cstr(const T& text) {
  return text.c_str();
}

template <typename V, typename U>
inline void show(lv_obj_t* value_label, lv_obj_t*& unit_label, const V& value, const U& unit) {
  show(value_label, unit_label, cstr(value), cstr(unit));
}
template <typename U>
inline bool reserve(lv_obj_t* value_label, lv_obj_t*& unit_label, const U& unit) {
  return reserve(value_label, unit_label, cstr(unit));
}
template <typename V, typename U>
inline void show(lv_obj_t* value_label, const V& value, const U& unit) {
  show(value_label, cstr(value), cstr(unit));
}
template <typename T>
inline void show_text(lv_obj_t* value_label, const T& text) {
  show_text(value_label, cstr(text));
}

}  // namespace value_unit
