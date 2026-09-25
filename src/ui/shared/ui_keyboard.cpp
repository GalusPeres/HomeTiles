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

// An explicit keyboard layout setting takes precedence; "Auto" follows
// the UI language: German/QWERTZ for umlauts and sharp S, otherwise
// English/QWERTY.
struct KeyboardLayout {
  const char* const* lower_map;
  const char* const* upper_map;
  const lv_buttonmatrix_ctrl_t* ctrl_map;
};

const KeyboardLayout* layout_for_config(uint8_t keyboard_layout, const char* lang_code) {
  bool german;
  if (keyboard_layout == 1) {
    german = true;   // Force German (QWERTZ).
  } else if (keyboard_layout == 2) {
    german = false;  // Force English (QWERTY).
  } else {
    german = lang_code && lang_code[0] == 'd' && lang_code[1] == 'e';
  }
  if (german) {
    static const KeyboardLayout kDeLayout{kMapLowerDe, kMapUpperDe, kCtrlDe};
    return &kDeLayout;
  }
  static const KeyboardLayout kEnLayout{kMapLowerEn, kMapUpperEn, kCtrlEn};
  return &kEnLayout;
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
  const bool is_ctrl =
      lv_buttonmatrix_has_button_ctrl(kb, id, LV_BUTTONMATRIX_CTRL_CHECKED);
  const bool pressed = lv_obj_has_state(kb, LV_STATE_PRESSED) &&
                       lv_buttonmatrix_get_selected_button(kb) == id;

  uint32_t color;
  if (is_ok) {
    color = pressed ? kKeyBgOkPressed : kKeyBgOk;
  } else if (pressed) {
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
  const KeyboardLayout* layout = layout_for_config(kb_cfg.keyboard_layout, kb_cfg.language);
  lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_LOWER, layout->lower_map, layout->ctrl_map);
  lv_keyboard_set_map(kb, LV_KEYBOARD_MODE_TEXT_UPPER, layout->upper_map, layout->ctrl_map);

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
  lv_keyboard_set_textarea(kb, ta);
  // Set focus explicitly so the textarea shows its cursor even when the
  // user taps directly on the keyboard instead of the field.
  if (prev && prev != ta) lv_obj_remove_state(prev, LV_STATE_FOCUSED);
  lv_obj_add_state(ta, LV_STATE_FOCUSED);
}
