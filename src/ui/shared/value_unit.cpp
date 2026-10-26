#include "src/ui/shared/value_unit.h"

#include <string.h>

#include <string>

#include "src/fonts/ui_fonts.h"

namespace value_unit {
namespace {

struct FontSize {
  const lv_font_t* font;
  uint8_t px;
};

// The panel's text fonts, ascending: a unit takes one of these (the larger
// sizes serve numbers and clocks).
const FontSize kTextFonts[] = {
#if defined(DEVICE_LAYOUT_480X480)
    {&ui_font_12, 12}, {&ui_font_14, 14},
#endif
    {&ui_font_16, 16}, {&ui_font_20, 20}, {&ui_font_24, 24}, {&ui_font_28, 28}, {&ui_font_32, 32},
};
constexpr uint8_t kTextFontCount = sizeof(kTextFonts) / sizeof(kTextFonts[0]);

// Every size a number may take.
const FontSize kValueFonts[] = {
#if defined(DEVICE_LAYOUT_480X480)
    {&ui_font_12, 12}, {&ui_font_14, 14},
#endif
    {&ui_font_16, 16}, {&ui_font_20, 20}, {&ui_font_24, 24}, {&ui_font_28, 28}, {&ui_font_32, 32},
    {&ui_font_40, 40}, {&ui_font_48, 48}, {&ui_font_56, 56}, {&ui_font_64, 64}, {&ui_font_72, 72},
    {&ui_font_80, 80}, {&ui_font_96, 96},
};

// A unit label carries this tag as its user data, so its value label finds
// it again (the LVGL user flags are all taken).
const char kUnitTag = 0;

uint8_t px_of(const lv_font_t* font) {
  for (const FontSize& size : kValueFonts) {
    if (size.font == font) return size.px;
  }
  return 0;
}

// A digit's top below the top of a label line in `font`.
int32_t digit_top(const lv_font_t* font) {
  lv_font_glyph_dsc_t glyph;
  if (!font || !lv_font_get_glyph_dsc(font, &glyph, '0', 0)) return 0;
  return (static_cast<int32_t>(font->line_height) - font->base_line) - (glyph.box_h + glyph.ofs_y);
}

int32_t text_width(const char* text, const lv_font_t* font, int32_t letter_space) {
  lv_point_t size;
  lv_text_get_size(&size, text, font, letter_space, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  return size.x;
}

const lv_font_t* font_of(lv_obj_t* label) { return lv_obj_get_style_text_font(label, LV_PART_MAIN); }

int32_t gap_for(const lv_font_t* value_font, const lv_font_t* unit_font) {
  const uint8_t px = px_of(unit_font);
  return gap_px(px ? px : static_cast<uint8_t>(value_font->line_height * 3 / 4));
}

// The unit's room: its gap and width.
int32_t room_for(lv_obj_t* value_label, const char* unit) {
  const lv_font_t* font = font_of(value_label);
  const lv_font_t* small = unit_font(font);
  return gap_for(font, small) + text_width(unit, small, 0);
}

lv_obj_t* unit_child(lv_obj_t* value_label) {
  const uint32_t count = value_label ? lv_obj_get_child_count(value_label) : 0;
  for (uint32_t i = 0; i < count; ++i) {
    lv_obj_t* child = lv_obj_get_child(value_label, static_cast<int32_t>(i));
    if (lv_obj_get_user_data(child) == &kUnitTag) return child;
  }
  return nullptr;
}

void set_text(lv_obj_t* label, const char* text) {
  const char* shown = lv_label_get_text(label);
  if (!shown || strcmp(shown, text) != 0) lv_label_set_text(label, text);
}

void set_room(lv_obj_t* label, int32_t room) {
  if (lv_obj_get_style_pad_right(label, LV_PART_MAIN) != room) lv_obj_set_style_pad_right(label, room, 0);
}

}  // namespace

const uint8_t* unit_font_sizes(uint8_t& count) {
  static uint8_t sizes[kTextFontCount];
  for (uint8_t i = 0; i < kTextFontCount; ++i) sizes[i] = kTextFonts[i].px;
  count = kTextFontCount;
  return sizes;
}

const lv_font_t* unit_font(const lv_font_t* value_font) {
  const uint8_t px = px_of(value_font);
  if (!px) return value_font;
  uint8_t sizes[kTextFontCount];
  for (uint8_t i = 0; i < kTextFontCount; ++i) sizes[i] = kTextFonts[i].px;
  const uint8_t want = unit_px(px, sizes, kTextFontCount);
  for (const FontSize& size : kTextFonts) {
    if (size.px == want) return size.font;
  }
  return value_font;
}

bool reserve(lv_obj_t* value_label, lv_obj_t*& unit_label, const char* unit) {
  if (!value_label) return false;
  if (!unit || !unit[0]) {
    if (unit_label && !lv_obj_has_flag(unit_label, LV_OBJ_FLAG_HIDDEN)) lv_obj_add_flag(unit_label, LV_OBJ_FLAG_HIDDEN);
    set_room(value_label, 0);
    return false;
  }
  if (!unit_label) {
    unit_label = lv_label_create(value_label);
    lv_obj_set_user_data(unit_label, const_cast<char*>(&kUnitTag));
    // Floating: a content-sized value label counts only its text and the
    // room, not the unit again (LVGL adds its children's extent otherwise).
    lv_obj_add_flag(unit_label, LV_OBJ_FLAG_FLOATING);
    lv_obj_remove_flag(unit_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(unit_label, LV_OBJ_FLAG_SCROLLABLE);
  }
  const lv_font_t* small = unit_font(font_of(value_label));
  if (font_of(unit_label) != small) lv_obj_set_style_text_font(unit_label, small, 0);
  set_text(unit_label, unit);
  set_room(value_label, room_for(value_label, unit));
  if (lv_obj_has_flag(unit_label, LV_OBJ_FLAG_HIDDEN)) lv_obj_remove_flag(unit_label, LV_OBJ_FLAG_HIDDEN);
  return true;
}

void place(lv_obj_t* value_label, lv_obj_t* unit_label) {
  if (!value_label || !unit_label || lv_obj_has_flag(unit_label, LV_OBJ_FLAG_HIDDEN)) return;
  const lv_font_t* font = font_of(value_label);
  const lv_font_t* small = font_of(unit_label);
  const int32_t value_w = text_width(lv_label_get_text(value_label), font,
                                     lv_obj_get_style_text_letter_space(value_label, LV_PART_MAIN));
  const int32_t gap = gap_for(font, small);
  int32_t x = value_w + gap;
  if (lv_obj_get_style_width(value_label, LV_PART_MAIN) != LV_SIZE_CONTENT) {
    // A fixed width: the text keeps the unit's room on its right, so its
    // alignment places the pair.
    lv_obj_update_layout(value_label);
    const int32_t inner = lv_obj_get_content_width(value_label);
    const lv_text_align_t align = lv_obj_get_style_text_align(value_label, LV_PART_MAIN);
    if (align == LV_TEXT_ALIGN_CENTER) x = (inner - value_w) / 2 + value_w + gap;
    else if (align == LV_TEXT_ALIGN_RIGHT) x = inner + gap;
  }
  lv_obj_set_pos(unit_label, x, unit_y(digit_top(font), digit_top(small)));
}

void show(lv_obj_t* value_label, lv_obj_t*& unit_label, const char* value, const char* unit) {
  if (!value_label) return;
  value = value ? value : "";
  unit = unit ? unit : "";
  bool split = unit[0] != 0;
  if (split && lv_obj_get_style_width(value_label, LV_PART_MAIN) != LV_SIZE_CONTENT) {
    // Too wide for its label: one text, as before.
    lv_obj_update_layout(value_label);
    const int32_t width = lv_obj_get_width(value_label) - lv_obj_get_style_pad_left(value_label, LV_PART_MAIN);
    const int32_t value_w =
        text_width(value, font_of(value_label), lv_obj_get_style_text_letter_space(value_label, LV_PART_MAIN));
    split = value_w + room_for(value_label, unit) <= width;
  }
  if (!split) {
    reserve(value_label, unit_label, "");
    if (!unit[0]) {
      set_text(value_label, value);
    } else {
      const std::string text = std::string(value) + " " + unit;
      set_text(value_label, text.c_str());
    }
    return;
  }
  reserve(value_label, unit_label, unit);
  set_text(value_label, value);
  place(value_label, unit_label);
}

const char* unit_text(lv_obj_t* value_label) {
  lv_obj_t* unit = unit_child(value_label);
  if (!unit || lv_obj_has_flag(unit, LV_OBJ_FLAG_HIDDEN)) return "";
  return lv_label_get_text(unit);
}

void show(lv_obj_t* value_label, const char* value, const char* unit) {
  lv_obj_t* unit_label = unit_child(value_label);
  show(value_label, unit_label, value, unit);
}

void show_text(lv_obj_t* value_label, const char* text) {
  text = text ? text : "";
  int value_end = 0;
  const int start = split_unit(text, static_cast<int>(strlen(text)), value_end);
  if (start < 0) {
    show(value_label, text, "");
    return;
  }
  const std::string value(text, static_cast<size_t>(value_end));
  show(value_label, value.c_str(), text + start);
}

}  // namespace value_unit
