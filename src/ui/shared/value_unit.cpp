#include "src/ui/shared/value_unit.h"

#include <string.h>

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

void set_text(lv_obj_t* label, const char* text) {
  const char* shown = lv_label_get_text(label);
  if (!shown || strcmp(shown, text) != 0) lv_label_set_text(label, text);
}

void set_room(lv_obj_t* label, int32_t room) {
  if (lv_obj_get_style_pad_right(label, LV_PART_MAIN) != room) lv_obj_set_style_pad_right(label, room, 0);
}

}  // namespace

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

void show(lv_obj_t* value_label, lv_obj_t*& unit_label, const String& value, const String& unit) {
  if (!value_label) return;
  const lv_font_t* font = lv_obj_get_style_text_font(value_label, LV_PART_MAIN);
  const lv_font_t* small = unit.length() ? unit_font(font) : nullptr;
  int32_t room = 0;
  int32_t x = 0;
  if (small) {
    const int32_t value_w = text_width(value.c_str(), font, lv_obj_get_style_text_letter_space(value_label, LV_PART_MAIN));
    const uint8_t small_px = px_of(small);
    const int32_t gap = gap_px(small_px ? small_px : static_cast<uint8_t>(font->line_height * 3 / 4));
    room = gap + text_width(unit.c_str(), small, 0);
    if (lv_obj_get_style_width(value_label, LV_PART_MAIN) == LV_SIZE_CONTENT) {
      x = value_w + gap;
    } else {
      // A fixed width: the value's text keeps the room on its right, so its
      // alignment places the pair.
      lv_obj_update_layout(value_label);
      const int32_t width = lv_obj_get_width(value_label) - lv_obj_get_style_pad_left(value_label, LV_PART_MAIN);
      const int32_t inner = width - room;
      const lv_text_align_t align = lv_obj_get_style_text_align(value_label, LV_PART_MAIN);
      if (value_w > inner) {
        small = nullptr;
      } else if (align == LV_TEXT_ALIGN_CENTER) {
        x = (inner - value_w) / 2 + value_w + gap;
      } else if (align == LV_TEXT_ALIGN_RIGHT) {
        x = inner + gap;
      } else {
        x = value_w + gap;
      }
    }
  }
  if (!small) {
    if (unit_label) lv_obj_add_flag(unit_label, LV_OBJ_FLAG_HIDDEN);
    set_room(value_label, 0);
    set_text(value_label, unit.length() ? (value + " " + unit).c_str() : value.c_str());
    return;
  }
  if (!unit_label) {
    unit_label = lv_label_create(value_label);
    lv_obj_remove_flag(unit_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(unit_label, LV_OBJ_FLAG_SCROLLABLE);
  }
  if (lv_obj_get_style_text_font(unit_label, LV_PART_MAIN) != small) lv_obj_set_style_text_font(unit_label, small, 0);
  set_room(value_label, room);
  set_text(value_label, value.c_str());
  set_text(unit_label, unit.c_str());
  lv_obj_set_pos(unit_label, x, unit_y(digit_top(font), digit_top(small)));
  if (lv_obj_has_flag(unit_label, LV_OBJ_FLAG_HIDDEN)) lv_obj_remove_flag(unit_label, LV_OBJ_FLAG_HIDDEN);
}

}  // namespace value_unit
