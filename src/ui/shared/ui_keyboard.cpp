#include "src/ui/shared/ui_keyboard.h"

#include <cstring>

#include "src/core/config/config_manager.h"
#include "src/fonts/ui_fonts.h"
#include "src/ui/popups/popup_layout.h"

namespace {

// German letter layouts extend LVGL's standard map (lv_keyboard.c) with
// umlauts and sharp S, which German SSIDs, passwords and hostnames may
// need. Rows, key weights and special-key flags match LVGL exactly;
// only the three letter rows are extended.
// LV_BUTTONMATRIX_CTRL_POPOVER enlarges a pressed key only together with
// lv_keyboard_set_popovers(kb, true); see ui_keyboard_create. Otherwise
// lv_keyboard_update_ctrl_map() removes the flag from control maps
// installed through lv_keyboard_set_map().
// LV_BUTTONMATRIX_CTRL_CHECKED provides the permanently darker special-key
// appearance (see kb_draw_task_cb). Maps must remain static because the
// button matrix retains pointers instead of copying text.
// LVGL's flag enum has no fixed underlying type. In C++, unlike the C
// original in lv_keyboard.c, assigning "enum | int" narrows int to the
// enum and requires an explicit cast.
constexpr lv_buttonmatrix_ctrl_t kCtrl(int v) {
  return static_cast<lv_buttonmatrix_ctrl_t>(v);
}
constexpr lv_buttonmatrix_ctrl_t kBtn(uint8_t width) {
  return kCtrl(LV_BUTTONMATRIX_CTRL_POPOVER | width);
}

// "1#"/"ABC"/"abc" are lv_keyboard.c's mode-switch labels. Their macros
// are private there, so the same literals are used here.
static const char* const kMapLowerDe[] = {
    "1#", "q", "w", "e", "r", "t", "z", "u", "i", "o", "p", "\xC3\xBC", LV_SYMBOL_BACKSPACE, "\n",
    "ABC", "a", "s", "d", "f", "g", "h", "j", "k", "l", "\xC3\xB6", "\xC3\xA4", LV_SYMBOL_NEW_LINE, "\n",
    "_", "-", "y", "x", "c", "v", "b", "n", "m", "\xC3\x9F", ".", ",", ":", "\n",
    LV_SYMBOL_KEYBOARD, LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""};

static const lv_buttonmatrix_ctrl_t kCtrlDe[] = {
    kCtrl(LV_KEYBOARD_CTRL_BUTTON_FLAGS | 5), kBtn(4), kBtn(4), kBtn(4), kBtn(4), kBtn(4), kBtn(4), kBtn(4), kBtn(4), kBtn(4), kBtn(4), kBtn(4), kCtrl(LV_BUTTONMATRIX_CTRL_CHECKED | 7),
    kCtrl(LV_KEYBOARD_CTRL_BUTTON_FLAGS | 6), kBtn(3), kBtn(3), kBtn(3), kBtn(3), kBtn(3), kBtn(3), kBtn(3), kBtn(3), kBtn(3), kBtn(3), kBtn(3), kCtrl(LV_BUTTONMATRIX_CTRL_CHECKED | 7),
    kCtrl(LV_BUTTONMATRIX_CTRL_CHECKED | kBtn(1)), kCtrl(LV_BUTTONMATRIX_CTRL_CHECKED | kBtn(1)), kBtn(1), kBtn(1), kBtn(1), kBtn(1), kBtn(1), kBtn(1), kBtn(1), kBtn(1), kCtrl(LV_BUTTONMATRIX_CTRL_CHECKED | kBtn(1)), kCtrl(LV_BUTTONMATRIX_CTRL_CHECKED | kBtn(1)), kCtrl(LV_BUTTONMATRIX_CTRL_CHECKED | kBtn(1)),
    kCtrl(LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2), kCtrl(LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2), kCtrl(6), kCtrl(LV_BUTTONMATRIX_CTRL_CHECKED | 2), kCtrl(LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2)};

static const char* const kMapUpperDe[] = {
    "1#", "Q", "W", "E", "R", "T", "Z", "U", "I", "O", "P", "\xC3\x9C", LV_SYMBOL_BACKSPACE, "\n",
    "abc", "A", "S", "D", "F", "G", "H", "J", "K", "L", "\xC3\x96", "\xC3\x84", LV_SYMBOL_NEW_LINE, "\n",
    "_", "-", "Y", "X", "C", "V", "B", "N", "M", "\xC3\x9F", ".", ",", ":", "\n",
    LV_SYMBOL_KEYBOARD, LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""};

// English/QWERTY copies LVGL 9.5's default_kb_map_lc/uc and their identical
// control maps. Only the uppercase bottom-left key differs: LVGL uses
// LV_SYMBOL_CLOSE there, which ui_symbols_20/24 lack (it drew an empty box),
// so it uses LV_SYMBOL_KEYBOARD like the lowercase and German maps.
static const char* const kMapLowerEn[] = {
    "1#", "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", LV_SYMBOL_BACKSPACE, "\n",
    "ABC", "a", "s", "d", "f", "g", "h", "j", "k", "l", LV_SYMBOL_NEW_LINE, "\n",
    "_", "-", "z", "x", "c", "v", "b", "n", "m", ".", ",", ":", "\n",
    LV_SYMBOL_KEYBOARD, LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""};

static const lv_buttonmatrix_ctrl_t kCtrlEn[] = {
    kCtrl(LV_KEYBOARD_CTRL_BUTTON_FLAGS | 5), kBtn(4), kBtn(4), kBtn(4), kBtn(4), kBtn(4), kBtn(4), kBtn(4), kBtn(4), kBtn(4), kBtn(4), kCtrl(LV_BUTTONMATRIX_CTRL_CHECKED | 7),
    kCtrl(LV_KEYBOARD_CTRL_BUTTON_FLAGS | 6), kBtn(3), kBtn(3), kBtn(3), kBtn(3), kBtn(3), kBtn(3), kBtn(3), kBtn(3), kBtn(3), kCtrl(LV_BUTTONMATRIX_CTRL_CHECKED | 7),
    kCtrl(LV_BUTTONMATRIX_CTRL_CHECKED | kBtn(1)), kCtrl(LV_BUTTONMATRIX_CTRL_CHECKED | kBtn(1)), kBtn(1), kBtn(1), kBtn(1), kBtn(1), kBtn(1), kBtn(1), kBtn(1), kCtrl(LV_BUTTONMATRIX_CTRL_CHECKED | kBtn(1)), kCtrl(LV_BUTTONMATRIX_CTRL_CHECKED | kBtn(1)), kCtrl(LV_BUTTONMATRIX_CTRL_CHECKED | kBtn(1)),
    kCtrl(LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2), kCtrl(LV_BUTTONMATRIX_CTRL_CHECKED | 2), kCtrl(6), kCtrl(LV_BUTTONMATRIX_CTRL_CHECKED | 2), kCtrl(LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2)};

static const char* const kMapUpperEn[] = {
    "1#", "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", LV_SYMBOL_BACKSPACE, "\n",
    "abc", "A", "S", "D", "F", "G", "H", "J", "K", "L", LV_SYMBOL_NEW_LINE, "\n",
    "_", "-", "Z", "X", "C", "V", "B", "N", "M", ".", ",", ":", "\n",
    LV_SYMBOL_KEYBOARD, LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""};

// Polish keeps QWERTY, like Polish hardware keyboards ("Polski
// programisty"), which type the Polish letters with AltGr. The keyboard key
// (lower left) acts as AltGr: it swaps the letter maps for these, with each
// Polish letter on its AltGr key (a, c, e, l, n, o, s, x, z).
static const char* const kMapLowerPlAlt[] = {
    "1#", "q", "w", "\xC4\x99", "r", "t", "y", "u", "i", "\xC3\xB3", "p", LV_SYMBOL_BACKSPACE, "\n",
    "ABC", "\xC4\x85", "\xC5\x9B", "d", "f", "g", "h", "j", "k", "\xC5\x82", LV_SYMBOL_NEW_LINE, "\n",
    "_", "-", "\xC5\xBC", "\xC5\xBA", "\xC4\x87", "v", "b", "\xC5\x84", "m", ".", ",", ":", "\n",
    LV_SYMBOL_KEYBOARD, LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""};

static const char* const kMapUpperPlAlt[] = {
    "1#", "Q", "W", "\xC4\x98", "R", "T", "Y", "U", "I", "\xC3\x93", "P", LV_SYMBOL_BACKSPACE, "\n",
    "abc", "\xC4\x84", "\xC5\x9A", "D", "F", "G", "H", "J", "K", "\xC5\x81", LV_SYMBOL_NEW_LINE, "\n",
    "_", "-", "\xC5\xBB", "\xC5\xB9", "\xC4\x86", "V", "B", "\xC5\x83", "M", ".", ",", ":", "\n",
    LV_SYMBOL_KEYBOARD, LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""};

// French uses AZERTY like French hardware keyboards: A/Q and Z/W swapped,
// M beside L. The keyboard key swaps the letter rows for the French accents
// and quotes, which sit on their own keys there.
static const char* const kMapLowerFr[] = {
    "1#", "a", "z", "e", "r", "t", "y", "u", "i", "o", "p", LV_SYMBOL_BACKSPACE, "\n",
    "ABC", "q", "s", "d", "f", "g", "h", "j", "k", "l", "m", LV_SYMBOL_NEW_LINE, "\n",
    "_", "-", "w", "x", "c", "v", "b", "n", ".", ",", ":", "\n",
    LV_SYMBOL_KEYBOARD, LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""};

static const lv_buttonmatrix_ctrl_t kCtrlFr[] = {
    kCtrl(LV_KEYBOARD_CTRL_BUTTON_FLAGS | 5), kBtn(4), kBtn(4), kBtn(4), kBtn(4), kBtn(4), kBtn(4), kBtn(4), kBtn(4), kBtn(4), kBtn(4), kCtrl(LV_BUTTONMATRIX_CTRL_CHECKED | 7),
    kCtrl(LV_KEYBOARD_CTRL_BUTTON_FLAGS | 6), kBtn(3), kBtn(3), kBtn(3), kBtn(3), kBtn(3), kBtn(3), kBtn(3), kBtn(3), kBtn(3), kBtn(3), kCtrl(LV_BUTTONMATRIX_CTRL_CHECKED | 7),
    kCtrl(LV_BUTTONMATRIX_CTRL_CHECKED | kBtn(1)), kCtrl(LV_BUTTONMATRIX_CTRL_CHECKED | kBtn(1)), kBtn(1), kBtn(1), kBtn(1), kBtn(1), kBtn(1), kBtn(1), kCtrl(LV_BUTTONMATRIX_CTRL_CHECKED | kBtn(1)), kCtrl(LV_BUTTONMATRIX_CTRL_CHECKED | kBtn(1)), kCtrl(LV_BUTTONMATRIX_CTRL_CHECKED | kBtn(1)),
    kCtrl(LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2), kCtrl(LV_BUTTONMATRIX_CTRL_CHECKED | 2), kCtrl(6), kCtrl(LV_BUTTONMATRIX_CTRL_CHECKED | 2), kCtrl(LV_KEYBOARD_CTRL_BUTTON_FLAGS | 2)};

static const char* const kMapUpperFr[] = {
    "1#", "A", "Z", "E", "R", "T", "Y", "U", "I", "O", "P", LV_SYMBOL_BACKSPACE, "\n",
    "abc", "Q", "S", "D", "F", "G", "H", "J", "K", "L", "M", LV_SYMBOL_NEW_LINE, "\n",
    "_", "-", "W", "X", "C", "V", "B", "N", ".", ",", ":", "\n",
    LV_SYMBOL_KEYBOARD, LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""};

static const char* const kMapLowerFrAlt[] = {
    "1#", "\xC3\xA0", "\xC3\xA2", "\xC3\xA6", "\xC3\xA9", "\xC3\xA8", "\xC3\xAA", "\xC3\xAB", "\xC3\xAE", "\xC3\xAF", "\xC3\xB4", LV_SYMBOL_BACKSPACE, "\n",
    "ABC", "\xC3\xA7", "\xC5\x93", "\xC3\xB9", "\xC3\xBB", "\xC3\xBC", "\xC3\xBF", "\xC2\xAB", "\xC2\xBB", "\xE2\x80\x99", "\xE2\x82\xAC", LV_SYMBOL_NEW_LINE, "\n",
    "_", "-", "w", "x", "c", "v", "b", "n", ".", ",", ":", "\n",
    LV_SYMBOL_KEYBOARD, LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""};

static const char* const kMapUpperFrAlt[] = {
    "1#", "\xC3\x80", "\xC3\x82", "\xC3\x86", "\xC3\x89", "\xC3\x88", "\xC3\x8A", "\xC3\x8B", "\xC3\x8E", "\xC3\x8F", "\xC3\x94", LV_SYMBOL_BACKSPACE, "\n",
    "abc", "\xC3\x87", "\xC5\x92", "\xC3\x99", "\xC3\x9B", "\xC3\x9C", "\xC5\xB8", "\xC2\xAB", "\xC2\xBB", "\xE2\x80\x99", "\xE2\x82\xAC", LV_SYMBOL_NEW_LINE, "\n",
    "_", "-", "W", "X", "C", "V", "B", "N", ".", ",", ":", "\n",
    LV_SYMBOL_KEYBOARD, LV_SYMBOL_LEFT, " ", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""};

// An explicit keyboard layout setting takes precedence; "Auto" follows
// the UI language: German/QWERTZ for umlauts and sharp S, Polish/QWERTY and
// French/AZERTY with their letters behind the keyboard key, otherwise
// English/QWERTY. alt maps share ctrl_map; nullptr when the layout has none.
struct KeyboardLayout {
  const char* const* lower_map;
  const char* const* upper_map;
  const lv_buttonmatrix_ctrl_t* ctrl_map;
  const char* const* alt_lower_map;
  const char* const* alt_upper_map;
};

const KeyboardLayout* layout_for_config(uint8_t keyboard_layout, const char* lang_code) {
  const bool auto_layout = keyboard_layout != 1 && keyboard_layout != 2;
  const auto lang = [lang_code](char a, char b) {
    return lang_code && lang_code[0] == a && lang_code[1] == b;
  };
  if (keyboard_layout == 1 || (auto_layout && lang('d', 'e'))) {
    static const KeyboardLayout kDeLayout{kMapLowerDe, kMapUpperDe, kCtrlDe, nullptr, nullptr};
    return &kDeLayout;
  }
  if (auto_layout && lang('p', 'l')) {
    static const KeyboardLayout kPlLayout{kMapLowerEn, kMapUpperEn, kCtrlEn, kMapLowerPlAlt, kMapUpperPlAlt};
    return &kPlLayout;
  }
  if (auto_layout && lang('f', 'r')) {
    static const KeyboardLayout kFrLayout{kMapLowerFr, kMapUpperFr, kCtrlFr, kMapLowerFrAlt, kMapUpperFrAlt};
    return &kFrLayout;
  }
  static const KeyboardLayout kEnLayout{kMapLowerEn, kMapUpperEn, kCtrlEn, nullptr, nullptr};
  return &kEnLayout;
}

// The keyboard's layout and whether the keyboard key switched to its alt
// letters. One keyboard exists at a time (the settings popup).
const KeyboardLayout* g_layout = nullptr;
bool g_alt_letters = false;

void install_letter_maps(lv_obj_t* kb, bool alt) {
  if (!g_layout) return;
  g_alt_letters = alt && g_layout->alt_lower_map;
  lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_LOWER,
                      g_alt_letters ? g_layout->alt_lower_map : g_layout->lower_map,
                      g_layout->ctrl_map);
  lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_UPPER,
                      g_alt_letters ? g_layout->alt_upper_map : g_layout->upper_map,
                      g_layout->ctrl_map);
}

// LVGL's keyboard key sends LV_EVENT_CANCEL (collapse). On letter pages of a
// layout with alt letters it switches them instead, and the collapse
// handlers of the owner do not run; a tap outside the field still collapses.
void kb_alt_letters_cb(lv_event_t* e) {
  lv_obj_t* kb = static_cast<lv_obj_t*>(lv_event_get_target(e));
  const lv_keyboard_mode_t mode = lv_keyboard_get_mode(kb);
  if (!g_layout || !g_layout->alt_lower_map ||
      (mode != LV_KEYBOARD_MODE_TEXT_LOWER && mode != LV_KEYBOARD_MODE_TEXT_UPPER)) {
    return;
  }
  install_letter_maps(kb, !g_alt_letters);
  lv_event_stop_processing(e);
}

constexpr uint32_t kKeyBg = 0x3A3A3A;
constexpr uint32_t kKeyBgCtrl = 0x343034;      // RGB565-neutral medium gray for control keys
constexpr uint32_t kKeyBgPressed = 0x5A5A5A;
// Use the same green as other confirmation actions: connect/save/update.
constexpr uint32_t kKeyBgOk = 0x2E7D32;
constexpr uint32_t kKeyBgOkPressed = 0x43A047;
constexpr uint32_t kKeyText = 0xEDEDED;

// Button-matrix items have no individual styles. LVGL's default theme
// adds an accent outline to the focused key, which changes constantly
// while typing. Its state selector is more specific than our stateless
// local style and therefore wins: LVGL chooses the most specific state
// match, not always the local style. This caused the reported green tint.
// Force all key colors and borders/outlines in the draw-task event:
// dark-gray letters, darker control keys, a green OK key, white text,
// and no border or outline on any key.
void kb_draw_task_cb(lv_event_t* e) {
  lv_draw_task_t* task = lv_event_get_draw_task(e);
  if (!task) return;
  lv_draw_dsc_base_t* base =
      static_cast<lv_draw_dsc_base_t*>(lv_draw_task_get_draw_dsc(task));
  if (!base || base->part != LV_PART_ITEMS) return;

  lv_obj_t* kb = static_cast<lv_obj_t*>(lv_event_get_target(e));
  const uint32_t id = base->id1;
  const char* txt = lv_buttonmatrix_get_button_text(kb, id);

  lv_draw_label_dsc_t* label = lv_draw_task_get_label_dsc(task);
  if (label) {
    label->color = lv_color_hex(kKeyText);
    // Special keys (arrows, backspace, enter, keyboard switch, OK) use
    // LV_SYMBOL_* codepoints in LVGL's private Unicode range (UTF-8 lead
    // byte 0xEF). ui_font_20/24 contain text only and would show missing-glyph
    // boxes, so these keys use a small font containing the required symbols.
    if (txt && static_cast<unsigned char>(txt[0]) == 0xEF) {
#if defined(DEVICE_LAYOUT_1024X600)
      label->font = &ui_symbols_20;
#elif defined(DEVICE_LAYOUT_480X480)
      label->font = &lv_font_montserrat_14;
#else
      const bool large =
          lv_display_get_horizontal_resolution(nullptr) >= 1024;
      label->font =
          large ? &ui_symbols_24 : &ui_symbols_20;
#endif
    }
    return;
  }

  // Both borders and outlines use LV_DRAW_TASK_TYPE_BORDER. Suppress them
  // regardless of which theme state requested them.
  lv_draw_border_dsc_t* border = lv_draw_task_get_border_dsc(task);
  if (border) {
    border->opa = LV_OPA_TRANSP;
    return;
  }

  // The theme also adds a gray, Y-offset box shadow to each key. Its state
  // selector can outrank our local shadow_opa style, so suppress it directly
  // like borders/outlines instead of relying on style precedence.
  lv_draw_box_shadow_dsc_t* shadow = lv_draw_task_get_box_shadow_dsc(task);
  if (shadow) {
    shadow->opa = LV_OPA_TRANSP;
    return;
  }

  lv_draw_fill_dsc_t* fill = lv_draw_task_get_fill_dsc(task);
  if (!fill) return;

  const bool is_ok = txt && strcmp(txt, LV_SYMBOL_OK) == 0;
  const bool alt_on = g_alt_letters && txt && strcmp(txt, LV_SYMBOL_KEYBOARD) == 0;
  const bool is_ctrl =
      lv_buttonmatrix_has_button_ctrl(kb, id, LV_BUTTONMATRIX_CTRL_CHECKED);
  const bool pressed = lv_obj_has_state(kb, LV_STATE_PRESSED) &&
                       lv_buttonmatrix_get_selected_button(kb) == id;

  uint32_t color;
  if (is_ok) {
    color = pressed ? kKeyBgOkPressed : kKeyBgOk;
  } else if (pressed || alt_on) {
    color = kKeyBgPressed;
  } else if (is_ctrl) {
    color = kKeyBgCtrl;
  } else {
    color = kKeyBg;
  }
  fill->color = lv_color_hex(color);
  fill->opa = LV_OPA_COVER;
  fill->grad.dir = LV_GRAD_DIR_NONE;
  fill->grad.stops[0].color = fill->color;
  fill->grad.stops[1].color = fill->color;
  fill->grad.stops[0].opa = LV_OPA_COVER;
  fill->grad.stops[1].opa = LV_OPA_COVER;
}

}  // namespace

lv_obj_t* ui_keyboard_create(lv_obj_t* parent) {
  lv_obj_t* kb = lv_keyboard_create(parent);

  // Borderless and transparent: the card background shows between keys,
  // and the keys extend to the card edge.
  lv_obj_set_style_bg_opa(kb, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_opa(kb, LV_OPA_TRANSP, 0);
  lv_obj_set_style_pad_all(kb, 0, 0);
  lv_obj_set_style_pad_gap(kb, popup_layout::scale(6), 0);
  lv_obj_set_style_radius(kb, 0, 0);

  // Use our fonts: LVGL's built-in Montserrat fonts cover ASCII and symbols,
  // while ui_font_20/24 also include umlauts and sharp S (range 174-383).
#if defined(DEVICE_LAYOUT_1024X600)
  lv_obj_set_style_text_font(kb, popup_layout::font24(), LV_PART_ITEMS);
#elif defined(DEVICE_LAYOUT_480X480)
  lv_obj_set_style_text_font(kb, &ui_font_14, LV_PART_ITEMS);
#else
  const bool large = lv_display_get_horizontal_resolution(nullptr) >= 1024;
  lv_obj_set_style_text_font(
      kb, large ? &ui_font_24 : &ui_font_20, LV_PART_ITEMS);
#endif

  // Always install both letter maps; LVGL's default uppercase map has a
  // glyph our symbol fonts lack.
  const DeviceConfig& kb_cfg = configManager.getConfig();
  g_layout = layout_for_config(kb_cfg.keyboard_layout, kb_cfg.language);
  install_letter_maps(kb, false);
  // Before the owner's handlers: the keyboard key may switch letters instead.
  lv_obj_add_event_cb(kb, kb_alt_letters_cb, LV_EVENT_CANCEL, nullptr);

  lv_obj_set_style_bg_color(kb, lv_color_hex(kKeyBg), LV_PART_ITEMS);
  lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, LV_PART_ITEMS);
  lv_obj_set_style_text_color(kb, lv_color_hex(kKeyText), LV_PART_ITEMS);
  lv_obj_set_style_radius(kb, popup_layout::scale(10), LV_PART_ITEMS);
  lv_obj_set_style_border_opa(kb, LV_OPA_TRANSP, LV_PART_ITEMS);
  lv_obj_set_style_outline_opa(kb, LV_OPA_TRANSP, LV_PART_ITEMS);
  lv_obj_set_style_shadow_opa(kb, LV_OPA_TRANSP, LV_PART_ITEMS);
  // Control keys carry LV_BUTTONMATRIX_CTRL_CHECKED in the keyboard map.
  lv_obj_set_style_bg_color(kb, lv_color_hex(kKeyBgCtrl),
                            LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_bg_color(kb, lv_color_hex(kKeyBgPressed),
                            LV_PART_ITEMS | LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(kb, lv_color_hex(kKeyBgPressed),
                            LV_PART_ITEMS | LV_STATE_CHECKED | LV_STATE_PRESSED);

  lv_obj_add_flag(kb, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);
  lv_obj_add_event_cb(kb, kb_draw_task_cb, LV_EVENT_DRAW_TASK_ADDED, nullptr);

  // Enlarge the key preview while held, as on Android/iOS. Without this
  // call, LVGL removes the POPOVER flags from the control maps above.
  lv_keyboard_set_popovers(kb, true);
  return kb;
}

void ui_keyboard_set_target(lv_obj_t* kb, lv_obj_t* ta, lv_obj_t* prev) {
  if (!kb || !ta) return;
  // Every field starts on the normal letters.
  if (g_alt_letters) install_letter_maps(kb, false);
  lv_keyboard_set_textarea(kb, ta);
  // Set focus explicitly so the textarea shows its cursor even when the
  // user taps directly on the keyboard instead of the field.
  if (prev && prev != ta) lv_obj_remove_state(prev, LV_STATE_FOCUSED);
  lv_obj_add_state(ta, LV_STATE_FOCUSED);
}
