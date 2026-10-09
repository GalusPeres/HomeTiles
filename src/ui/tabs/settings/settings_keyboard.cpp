#include "src/ui/tabs/settings/settings_keyboard.h"

#include <cmath>
#include <cstring>

#include "src/tiles/icons/mdi_icons.h"
#include "src/ui/tabs/settings/settings_parts.h"
#include "src/ui/shared/ui_theme.h"

namespace settings_keyboard {
namespace {

using settings_style::Colors;

// The letter and symbol rows (mockup keyboard()). Row 2 holds up to ten
// keys, row 3 up to seven between Shift and Backspace.
struct Page {
  const char* const* rows[3];
  uint8_t counts[3];
};

const char* const kQwerty1[] = {"q", "w", "e", "r", "t", "y", "u", "i", "o", "p"};
const char* const kQwertz1[] = {"q", "w", "e", "r", "t", "z", "u", "i", "o", "p"};
const char* const kAzerty1[] = {"a", "z", "e", "r", "t", "y", "u", "i", "o", "p"};
const char* const kQwerty2[] = {"a", "s", "d", "f", "g", "h", "j", "k", "l"};
const char* const kAzerty2[] = {"q", "s", "d", "f", "g", "h", "j", "k", "l", "m"};
const char* const kQwerty3[] = {"z", "x", "c", "v", "b", "n", "m"};
const char* const kQwertz3[] = {"y", "x", "c", "v", "b", "n", "m"};
const char* const kAzerty3[] = {"w", "x", "c", "v", "b", "n"};
const char* const kSymbols1[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"};
const char* const kSymbols2[] = {"@", "#", "$", "%", "&", "*", "-", "+", "("};
const char* const kSymbols3[] = {"!", "\"", "'", ":", ";", "/", "?"};
// Every other ASCII symbol, then a few common ones from the panel's font.
const char* const kMore1[] = {")", "_", "=", "[", "]", "{", "}", "<", ">", "\\"};
const char* const kMore2[] = {"^", "`", "|", "~", ",", "\xE2\x82\xAC", "\xC2\xA3", "\xC2\xA5", "\xC2\xB0"};
const char* const kMore3[] = {"\xC2\xA7", "\xC2\xBF", "\xC2\xA1", "\xC2\xAB", "\xC2\xBB", "\xE2\x80\xA2", "\xE2\x80\x99"};

const Page kPages[] = {
    {{kQwerty1, kQwerty2, kQwerty3}, {10, 9, 7}},
    {{kQwertz1, kQwerty2, kQwertz3}, {10, 9, 7}},
    {{kAzerty1, kAzerty2, kAzerty3}, {10, 10, 6}},
    {{kSymbols1, kSymbols2, kSymbols3}, {10, 9, 7}},
    {{kMore1, kMore2, kMore3}, {10, 9, 7}},
};

// The accent key's pages: the language's accented letters first, then other
// common ones, lower and upper case (Shift). German and other European
// languages, French (as the former keyboard's French page), Polish.
struct AccentPage {
  const char* label;  // on the accent key
  const char* const* lower[3];
  const char* const* upper[3];
  uint8_t counts[3];
};
const char* const kEuropeanLower1[] = {"\xC3\xA4", "\xC3\xB6", "\xC3\xBC", "\xC3\x9F", "\xC3\xA9",
                                       "\xC3\xA8", "\xC3\xAA", "\xC3\xA0", "\xC3\xA2", "\xC3\xA7"};
const char* const kEuropeanLower2[] = {"\xC3\xAB", "\xC3\xAE", "\xC3\xAF", "\xC3\xB4", "\xC3\xBB",
                                       "\xC3\xB9", "\xC3\xB1", "\xC5\x93", "\xC3\xA6"};
const char* const kEuropeanLower3[] = {"\xC3\xA1", "\xC3\xAD", "\xC3\xB3", "\xC3\xBA", "\xC3\xBF", "\xC3\xB8", "\xC3\xA5"};
const char* const kEuropeanUpper1[] = {"\xC3\x84", "\xC3\x96", "\xC3\x9C", "\xC3\x9F", "\xC3\x89",
                                       "\xC3\x88", "\xC3\x8A", "\xC3\x80", "\xC3\x82", "\xC3\x87"};
const char* const kEuropeanUpper2[] = {"\xC3\x8B", "\xC3\x8E", "\xC3\x8F", "\xC3\x94", "\xC3\x9B",
                                       "\xC3\x99", "\xC3\x91", "\xC5\x92", "\xC3\x86"};
const char* const kEuropeanUpper3[] = {"\xC3\x81", "\xC3\x8D", "\xC3\x93", "\xC3\x9A", "\xC5\xB8", "\xC3\x98", "\xC3\x85"};
const char* const kFrenchLower1[] = {"\xC3\xA0", "\xC3\xA2", "\xC3\xA6", "\xC3\xA9", "\xC3\xA8",
                                     "\xC3\xAA", "\xC3\xAB", "\xC3\xAE", "\xC3\xAF", "\xC3\xB4"};
const char* const kFrenchLower2[] = {"\xC3\xA7", "\xC5\x93", "\xC3\xB9", "\xC3\xBB", "\xC3\xBC",
                                     "\xC3\xBF", "\xC2\xAB", "\xC2\xBB", "\xE2\x80\x99", "\xE2\x82\xAC"};
const char* const kFrenchLower3[] = {"\xC3\xA4", "\xC3\xB6", "\xC3\x9F", "\xC3\xB1", "\xC3\xA1", "\xC3\xAD", "\xC3\xB3"};
const char* const kFrenchUpper1[] = {"\xC3\x80", "\xC3\x82", "\xC3\x86", "\xC3\x89", "\xC3\x88",
                                     "\xC3\x8A", "\xC3\x8B", "\xC3\x8E", "\xC3\x8F", "\xC3\x94"};
const char* const kFrenchUpper2[] = {"\xC3\x87", "\xC5\x92", "\xC3\x99", "\xC3\x9B", "\xC3\x9C",
                                     "\xC5\xB8", "\xC2\xAB", "\xC2\xBB", "\xE2\x80\x99", "\xE2\x82\xAC"};
const char* const kFrenchUpper3[] = {"\xC3\x84", "\xC3\x96", "\xC3\x9F", "\xC3\x91", "\xC3\x81", "\xC3\x8D", "\xC3\x93"};
const char* const kPolishLower1[] = {"\xC4\x85", "\xC4\x87", "\xC4\x99", "\xC5\x82", "\xC5\x84",
                                     "\xC3\xB3", "\xC5\x9B", "\xC5\xBA", "\xC5\xBC"};
const char* const kPolishLower2[] = {"\xC3\xA4", "\xC3\xB6", "\xC3\xBC", "\xC3\x9F", "\xC3\xA9",
                                     "\xC3\xA8", "\xC3\xA0", "\xC3\xA7", "\xC3\xB1"};
const char* const kPolishLower3[] = {"\xC3\xAA", "\xC3\xA2", "\xC3\xAE", "\xC3\xB4", "\xC3\xBB", "\xC3\xAB", "\xC3\xAF"};
const char* const kPolishUpper1[] = {"\xC4\x84", "\xC4\x86", "\xC4\x98", "\xC5\x81", "\xC5\x83",
                                     "\xC3\x93", "\xC5\x9A", "\xC5\xB9", "\xC5\xBB"};
const char* const kPolishUpper2[] = {"\xC3\x84", "\xC3\x96", "\xC3\x9C", "\xC3\x9F", "\xC3\x89",
                                     "\xC3\x88", "\xC3\x80", "\xC3\x87", "\xC3\x91"};
const char* const kPolishUpper3[] = {"\xC3\x8A", "\xC3\x82", "\xC3\x8E", "\xC3\x94", "\xC3\x9B", "\xC3\x8B", "\xC3\x8F"};

const AccentPage kAccentPages[] = {
    {"\xC3\xA4\xC3\xB6", {kEuropeanLower1, kEuropeanLower2, kEuropeanLower3},
     {kEuropeanUpper1, kEuropeanUpper2, kEuropeanUpper3}, {10, 9, 7}},
    {"\xC3\xA9\xC3\xA0", {kFrenchLower1, kFrenchLower2, kFrenchLower3}, {kFrenchUpper1, kFrenchUpper2, kFrenchUpper3},
     {10, 10, 7}},
    {"\xC4\x85\xC4\x99", {kPolishLower1, kPolishLower2, kPolishLower3}, {kPolishUpper1, kPolishUpper2, kPolishUpper3},
     {9, 9, 7}},
};

// Accented forms offered when a letter is held: German, Polish and French
// letters first, all within the panel font's Latin ranges.
struct Variants {
  char base;
  const char* lower[7];
  const char* upper[7];
};
const Variants kVariants[] = {
    {'a',
     {"\xC3\xA4", "\xC3\xA0", "\xC3\xA2", "\xC4\x85", "\xC3\xA6", "\xC3\xA1", "\xC3\xA5"},
     {"\xC3\x84", "\xC3\x80", "\xC3\x82", "\xC4\x84", "\xC3\x86", "\xC3\x81", "\xC3\x85"}},
    {'c', {"\xC3\xA7", "\xC4\x87"}, {"\xC3\x87", "\xC4\x86"}},
    {'e',
     {"\xC3\xA9", "\xC3\xA8", "\xC3\xAA", "\xC3\xAB", "\xC4\x99"},
     {"\xC3\x89", "\xC3\x88", "\xC3\x8A", "\xC3\x8B", "\xC4\x98"}},
    {'i', {"\xC3\xAE", "\xC3\xAF", "\xC3\xAD"}, {"\xC3\x8E", "\xC3\x8F", "\xC3\x8D"}},
    {'l', {"\xC5\x82"}, {"\xC5\x81"}},
    {'n', {"\xC5\x84", "\xC3\xB1"}, {"\xC5\x83", "\xC3\x91"}},
    {'o',
     {"\xC3\xB6", "\xC3\xB3", "\xC3\xB4", "\xC5\x93", "\xC3\xB8"},
     {"\xC3\x96", "\xC3\x93", "\xC3\x94", "\xC5\x92", "\xC3\x98"}},
    {'s', {"\xC3\x9F", "\xC5\x9B"}, {"\xC3\x9F", "\xC5\x9A"}},
    {'u',
     {"\xC3\xBC", "\xC3\xB9", "\xC3\xBB", "\xC3\xBA"},
     {"\xC3\x9C", "\xC3\x99", "\xC3\x9B", "\xC3\x9A"}},
    {'y', {"\xC3\xBF"}, {"\xC5\xB8"}},
    {'z', {"\xC5\xBC", "\xC5\xBA"}, {"\xC5\xBB", "\xC5\xB9"}},
};

enum Special : uint8_t { kShift = 40, kBackspace, kSymbols, kAccentKey, kSpace, kDot, kOk };

struct State {
  lv_obj_t* root = nullptr;
  Geometry g = {};
  Layout layout = Layout::Qwerty;
  Accents accent_set = Accents::European;
  Colors colors = {};
  uint32_t accent = 0;
  Handler handler = {};
  bool enabled = true;
  bool shift = false;
  bool symbols = false;
  bool more = false;
  // The accent page shows in the letter rows.
  bool accents = false;
  lv_obj_t* keys[3][10] = {};
  lv_obj_t* shift_key = nullptr;
  lv_obj_t* symbols_key = nullptr;
  lv_obj_t* accent_key = nullptr;
  // The accented forms of a held letter.
  lv_obj_t* popup = nullptr;
  const Variants* variants = nullptr;
  uint8_t variant_count = 0;
  int8_t variant = -1;
};
State g_kb;

float unit() { return (g_kb.g.width - 9.0f * g_kb.g.gap) / 10.0f; }

// A key `units` wide starting `start` units into the row: every unit of the
// grid is a key and a gap wide.
void place(lv_obj_t* key, int row, float start, float units) {
  const float u = unit();
  const float x = start * (u + g_kb.g.gap);
  const float w = units * u + (units - 1) * g_kb.g.gap;
  lv_obj_set_pos(key, static_cast<int>(lroundf(x)), row * g_kb.g.step);
  lv_obj_set_size(key, static_cast<int>(lroundf(w)), g_kb.g.key_h);
}

const Page& page() {
  if (g_kb.symbols) return kPages[g_kb.more ? 4 : 3];
  return kPages[static_cast<uint8_t>(g_kb.layout)];
}

void set_label(lv_obj_t* key, const char* text) {
  lv_obj_t* label = lv_obj_get_child(key, 0);
  if (label && strcmp(lv_label_get_text(label), text) != 0) lv_label_set_text(label, text);
}

// Letters uppercase with Shift (ASCII letters only; symbols stay).
void key_text(const char* text, char* out, size_t len) {
  snprintf(out, len, "%s", text);
  if (g_kb.shift && !g_kb.symbols && out[0] >= 'a' && out[0] <= 'z' && !out[1]) out[0] = static_cast<char>(out[0] - 32);
}

const AccentPage& accent_page() { return kAccentPages[static_cast<uint8_t>(g_kb.accent_set) % 3]; }

void lay_out() {
  const char* const* rows[3];
  uint8_t counts[3];
  if (g_kb.accents) {
    // Upper and lower case come from the page itself (Shift).
    const AccentPage& a = accent_page();
    for (int row = 0; row < 3; ++row) {
      rows[row] = g_kb.shift ? a.upper[row] : a.lower[row];
      counts[row] = a.counts[row];
    }
  } else {
    const Page& p = page();
    for (int row = 0; row < 3; ++row) {
      rows[row] = p.rows[row];
      counts[row] = p.counts[row];
    }
  }
  for (int row = 0; row < 3; ++row) {
    const int count = counts[row];
    // Shorter rows are centered on the grid; row 3 sits after Shift.
    const float start = row == 2 ? 1.5f : (10 - count) / 2.0f;
    for (int i = 0; i < 10; ++i) {
      lv_obj_t* key = g_kb.keys[row][i];
      if (!key) continue;
      if (i >= count) {
        lv_obj_add_flag(key, LV_OBJ_FLAG_HIDDEN);
        continue;
      }
      lv_obj_remove_flag(key, LV_OBJ_FLAG_HIDDEN);
      place(key, row, start + i, 1);
      char text[8];
      if (g_kb.accents) {
        snprintf(text, sizeof(text), "%s", rows[row][i]);
      } else {
        key_text(rows[row][i], text, sizeof(text));
      }
      set_label(key, text);
    }
  }
  if (g_kb.shift_key) {
    lv_obj_t* icon = lv_obj_get_child(g_kb.shift_key, 0);
    lv_obj_t* text = lv_obj_get_child(g_kb.shift_key, 1);
    // Letters: the Shift arrow; symbols: the way to the second page.
    if (icon) {
      lv_label_set_text(icon, getMdiChar(g_kb.shift ? "apple-keyboard-caps" : "apple-keyboard-shift").c_str());
      if (g_kb.symbols) {
        lv_obj_add_flag(icon, LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_remove_flag(icon, LV_OBJ_FLAG_HIDDEN);
      }
    }
    if (text) {
      lv_label_set_text(text, g_kb.more ? "123" : "#+=");
      if (g_kb.symbols) {
        lv_obj_remove_flag(text, LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_add_flag(text, LV_OBJ_FLAG_HIDDEN);
      }
    }
  }
  if (g_kb.symbols_key) set_label(g_kb.symbols_key, g_kb.symbols ? "ABC" : "?123");
  if (g_kb.accent_key) set_label(g_kb.accent_key, g_kb.accents ? "ABC" : accent_page().label);
}

const Variants* variants_for(const char* text) {
  if (g_kb.symbols || g_kb.accents || !text || !text[0] || text[1]) return nullptr;
  const char base = static_cast<char>(text[0] | 0x20);
  for (const Variants& v : kVariants) {
    if (v.base == base) return &v;
  }
  return nullptr;
}

void close_popup() {
  if (g_kb.popup) lv_obj_delete(g_kb.popup);
  g_kb.popup = nullptr;
  g_kb.variants = nullptr;
  g_kb.variant = -1;
}

const char* variant_text(uint8_t index) {
  return g_kb.shift ? g_kb.variants->upper[index] : g_kb.variants->lower[index];
}

void style_variant(lv_obj_t* cell, bool selected) {
  lv_obj_set_style_bg_color(cell, lv_color_hex(selected ? g_kb.accent : g_kb.colors.button), 0);
  lv_obj_t* label = lv_obj_get_child(cell, 0);
  if (label) {
    lv_obj_set_style_text_color(label, lv_color_hex(selected ? settings_style::kAccentText : ui_theme::text()), 0);
  }
}

// The accented forms in a row of keys above the held one; the first one is
// chosen until the finger moves.
void open_popup(lv_obj_t* key, const Variants* variants) {
  close_popup();
  uint8_t count = 0;
  while (count < 7 && variants->lower[count]) ++count;
  if (!count) return;
  g_kb.variants = variants;
  g_kb.variant_count = count;
  g_kb.variant = 0;
  const int cell = lv_obj_get_width(key);
  const int width = count * cell + (count + 1) * g_kb.g.gap;
  int x = lv_obj_get_x(key) - g_kb.g.gap;
  if (x + width > g_kb.g.width) x = g_kb.g.width - width;
  if (x < 0) x = 0;
  g_kb.popup = settings_parts::plain(g_kb.root);
  lv_obj_set_pos(g_kb.popup, x, lv_obj_get_y(key) - g_kb.g.key_h - 2 * g_kb.g.gap);
  lv_obj_set_size(g_kb.popup, width, g_kb.g.key_h + 2 * g_kb.g.gap);
  lv_obj_set_style_bg_color(g_kb.popup, lv_color_hex(g_kb.colors.card), 0);
  lv_obj_set_style_bg_opa(g_kb.popup, LV_OPA_COVER, 0);
  settings_style::apply_radius(g_kb.popup, g_kb.g.radius + g_kb.g.gap);
  ui_surface_style::apply_global_tile_border(g_kb.popup);
  for (uint8_t i = 0; i < count; ++i) {
    lv_obj_t* c = settings_parts::plain(g_kb.popup);
    lv_obj_set_pos(c, g_kb.g.gap + i * (cell + g_kb.g.gap), g_kb.g.gap);
    lv_obj_set_size(c, cell, g_kb.g.key_h);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    settings_style::apply_radius(c, g_kb.g.radius);
    lv_obj_t* label = lv_label_create(c);
    lv_label_set_text(label, variant_text(i));
    lv_obj_set_style_text_font(label, popup_layout::font28(), 0);
    lv_obj_center(label);
    style_variant(c, i == 0);
  }
}

void track_popup() {
  lv_indev_t* indev = lv_indev_active();
  if (!indev || !g_kb.popup) return;
  lv_point_t point;
  lv_indev_get_point(indev, &point);
  lv_area_t area;
  lv_obj_get_coords(g_kb.popup, &area);
  const int cell = lv_obj_get_width(lv_obj_get_child(g_kb.popup, 0)) + g_kb.g.gap;
  int index = (point.x - area.x1 - g_kb.g.gap / 2) / (cell > 0 ? cell : 1);
  if (index < 0) index = 0;
  if (index >= g_kb.variant_count) index = g_kb.variant_count - 1;
  if (index == g_kb.variant) return;
  style_variant(lv_obj_get_child(g_kb.popup, g_kb.variant), false);
  style_variant(lv_obj_get_child(g_kb.popup, index), true);
  g_kb.variant = static_cast<int8_t>(index);
}

void type(const char* text) {
  if (g_kb.handler.text) g_kb.handler.text(text);
  // Shift holds for one letter (mockup type()).
  if (g_kb.shift && !g_kb.symbols) {
    g_kb.shift = false;
    lay_out();
  }
}

void char_key_cb(lv_event_t* e) {
  lv_obj_t* key = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
  const lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t* label = lv_obj_get_child(key, 0);
  const char* text = label ? lv_label_get_text(label) : "";
  if (!g_kb.enabled) {
    close_popup();
    return;
  }
  if (code == LV_EVENT_SHORT_CLICKED) {
    type(text);
  } else if (code == LV_EVENT_LONG_PRESSED) {
    const Variants* variants = variants_for(text);
    if (variants) open_popup(key, variants);
  } else if (code == LV_EVENT_PRESSING) {
    track_popup();
  } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    if (g_kb.popup && g_kb.variant >= 0 && code == LV_EVENT_RELEASED) {
      char chosen[8];
      snprintf(chosen, sizeof(chosen), "%s", variant_text(static_cast<uint8_t>(g_kb.variant)));
      close_popup();
      type(chosen);
    }
    close_popup();
  }
}

void special_key_cb(lv_event_t* e) {
  if (!g_kb.enabled) return;
  const lv_event_code_t code = lv_event_get_code(e);
  const auto special = static_cast<Special>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  if (special == kBackspace) {
    // Held: keeps deleting.
    if ((code == LV_EVENT_SHORT_CLICKED || code == LV_EVENT_LONG_PRESSED || code == LV_EVENT_LONG_PRESSED_REPEAT) &&
        g_kb.handler.backspace) {
      g_kb.handler.backspace();
    }
    return;
  }
  if (code != LV_EVENT_SHORT_CLICKED) return;
  switch (special) {
    case kShift:
      if (g_kb.symbols) {
        g_kb.more = !g_kb.more;
      } else {
        g_kb.shift = !g_kb.shift;
      }
      lay_out();
      break;
    case kSymbols:
      g_kb.symbols = !g_kb.symbols;
      g_kb.accents = false;
      g_kb.more = false;
      g_kb.shift = false;
      lay_out();
      break;
    case kAccentKey:
      g_kb.accents = !g_kb.accents;
      g_kb.symbols = false;
      g_kb.more = false;
      g_kb.shift = false;
      lay_out();
      break;
    case kSpace:
      type(" ");
      break;
    case kDot:
      type(".");
      break;
    case kOk:
      if (g_kb.handler.ok) g_kb.handler.ok();
      break;
    default:
      break;
  }
}

void root_delete_cb(lv_event_t* e) {
  if (lv_event_get_current_target(e) == g_kb.root) g_kb = State();
}

// A key: rounded, in the button color (letters), the control color (Shift,
// Backspace, ?123) or the accent (check).
lv_obj_t* make_key(uint32_t fill, uint32_t pressed) {
  lv_obj_t* key = settings_parts::plain(g_kb.root);
  lv_obj_add_flag(key, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(key, lv_color_hex(fill), 0);
  lv_obj_set_style_bg_opa(key, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(key, lv_color_hex(pressed), LV_STATE_PRESSED);
  settings_style::apply_radius(key, g_kb.g.radius);
  lv_obj_set_flex_flow(key, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(key, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  return key;
}

lv_obj_t* key_label(lv_obj_t* key, const char* text, const lv_font_t* font, uint32_t color) {
  lv_obj_t* label = lv_label_create(key);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
  return label;
}

lv_obj_t* special_key(Special special, uint32_t fill, uint32_t pressed, int row, float start, float units) {
  lv_obj_t* key = make_key(fill, pressed);
  place(key, row, start, units);
  void* data = reinterpret_cast<void*>(static_cast<uintptr_t>(special));
  lv_obj_add_event_cb(key, special_key_cb, LV_EVENT_SHORT_CLICKED, data);
  if (special == kBackspace) {
    lv_obj_add_event_cb(key, special_key_cb, LV_EVENT_LONG_PRESSED, data);
    lv_obj_add_event_cb(key, special_key_cb, LV_EVENT_LONG_PRESSED_REPEAT, data);
  }
  return key;
}

lv_obj_t* icon_label(lv_obj_t* key, const char* icon, uint32_t color) {
  lv_obj_t* label = key_label(key, getMdiChar(icon).c_str(), FONT_MDI_ICONS ? FONT_MDI_ICONS : popup_layout::font28(),
                              color);
  return label;
}

}  // namespace

lv_obj_t* create(lv_obj_t* parent, const Geometry& geometry, Layout layout, Accents accents, const Colors& colors,
                 uint32_t accent, const Handler& handler) {
  if (g_kb.root) lv_obj_delete(g_kb.root);
  g_kb = State();
  g_kb.g = geometry;
  g_kb.layout = layout;
  g_kb.accent_set = accents;
  g_kb.colors = colors;
  g_kb.accent = accent & 0xFFFFFF;
  g_kb.handler = handler;
  g_kb.root = settings_parts::plain(parent);
  lv_obj_set_pos(g_kb.root, geometry.x, geometry.y);
  lv_obj_set_size(g_kb.root, geometry.width, 3 * geometry.step + geometry.key_h);
  // The accented forms of a held letter show above the rows.
  lv_obj_add_flag(g_kb.root, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  lv_obj_add_event_cb(g_kb.root, root_delete_cb, LV_EVENT_DELETE, nullptr);

  const lv_font_t* font = popup_layout::font28();
  for (int row = 0; row < 3; ++row) {
    for (int i = 0; i < 10; ++i) {
      if (row == 2 && i >= 7) break;
      lv_obj_t* key = make_key(colors.button, colors.pressed);
      key_label(key, "", font, ui_theme::text());
      for (lv_event_code_t code : {LV_EVENT_SHORT_CLICKED, LV_EVENT_LONG_PRESSED, LV_EVENT_PRESSING, LV_EVENT_RELEASED,
                                   LV_EVENT_PRESS_LOST}) {
        lv_obj_add_event_cb(key, char_key_cb, code, nullptr);
      }
      g_kb.keys[row][i] = key;
    }
  }
  // Shift and Backspace beside row 3; ?123, space, dot and check below.
  g_kb.shift_key = special_key(kShift, colors.group, colors.button, 2, 0, 1.5f);
  icon_label(g_kb.shift_key, "apple-keyboard-shift", ui_theme::text());
  key_label(g_kb.shift_key, "#+=", font, ui_theme::text());
  lv_obj_t* backspace = special_key(kBackspace, colors.group, colors.button, 2, 8.5f, 1.5f);
  icon_label(backspace, "backspace-outline", ui_theme::text());
  // The mode key's text is smaller (mockup: 20 px class-scaled, 16 px on the 480 class).
#if defined(DEVICE_LAYOUT_480X480)
  const lv_font_t* mode_font = settings_style::row_font();
#else
  const lv_font_t* mode_font = settings_style::small_font();
#endif
  g_kb.symbols_key = special_key(kSymbols, colors.group, colors.button, 3, 0, 1.5f);
  key_label(g_kb.symbols_key, "?123", mode_font, ui_theme::text());
  // The accent key beside it: the language's accented letters (or ABC back).
  g_kb.accent_key = special_key(kAccentKey, colors.group, colors.button, 3, 1.5f, 1);
  key_label(g_kb.accent_key, "", mode_font, ui_theme::text());
  special_key(kSpace, colors.button, colors.pressed, 3, 2.5f, 5);
  lv_obj_t* dot = special_key(kDot, colors.button, colors.pressed, 3, 7.5f, 1);
  key_label(dot, ".", font, ui_theme::text());
  lv_obj_t* ok = special_key(kOk, g_kb.accent, lv_color_to_u32(lv_color_lighten(lv_color_hex(g_kb.accent), 31)) & 0xFFFFFF,
                             3, 8.5f, 1.5f);
  icon_label(ok, "check-bold", settings_style::kAccentText);
  lay_out();
  return g_kb.root;
}

void set_enabled(lv_obj_t* keyboard, bool enabled) {
  if (!keyboard || keyboard != g_kb.root) return;
  g_kb.enabled = enabled;
  if (!enabled) close_popup();
}

}  // namespace settings_keyboard
