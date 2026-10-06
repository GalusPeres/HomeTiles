#include "src/ui/tabs/settings/settings_entry.h"

#include <cstdio>

#include "src/tiles/icons/mdi_icons.h"
#include "src/ui/tabs/settings/settings_keyboard.h"
#include "src/ui/tabs/settings/settings_parts.h"
#include "src/ui/tabs/settings/settings_style.h"

namespace settings_entry {
namespace {

using settings_style::Colors;

struct Entry {
  lv_obj_t* root = nullptr;
  lv_obj_t* name = nullptr;  // the network name field (Add network)
  lv_obj_t* password = nullptr;
  lv_obj_t* message = nullptr;
  // The moving dots after "Connecting" (a fixed width, so the text stays).
  lv_obj_t* dots = nullptr;
  lv_timer_t* dots_timer = nullptr;
  uint8_t dot_count = 0;
  lv_obj_t* keyboard = nullptr;
  lv_obj_t* focus = nullptr;
  bool manual = false;
  bool busy = false;
  bool setup = false;
  uint32_t card_color = 0;
  void (*closed)() = nullptr;
  char ssid[33] = {};
};
Entry g_entry;

struct Geometry {
  int pad;
  int column;
  int close;
  int close_x;
  int close_y;
  int field_y;
  int field_h;
  int message_y;
  int key_y;
  bool compact;  // the Settings card on the 480 class: a slim title row
  bool stacked;  // name above password
};

// Mockup sheetGeo: the Settings card or the setup's popup card.
Geometry geometry(int card_w, int card_h, bool setup) {
  Geometry g = {};
#if defined(DEVICE_LAYOUT_480X480)
  g.pad = settings_style::kEntryPad;
  g.column = card_w - 2 * g.pad;
  g.field_y = setup ? settings_style::kSetupFieldTop : settings_style::kEntryFieldTop;
  g.field_h = setup ? settings_style::kSetupFieldHeight : settings_style::kEntryFieldHeight;
  g.message_y = setup ? settings_style::kSetupMessageTop : settings_style::kEntryMessageTop;
  g.compact = !setup;
  g.stacked = false;
#else
  g.column = card_w - 2 * settings_style::kEntrySide;
  if (g.column > settings_style::kEntryColumn) g.column = settings_style::kEntryColumn;
  g.pad = (card_w - g.column) / 2;
  g.field_y = settings_style::kEntryFieldTop;
  g.field_h = settings_style::kEntryFieldHeight;
  g.compact = false;
#endif
  if (setup) {
    g.close = popup_layout::kCloseButtonSize;
    g.close_x = settings_parts::popup_close_x(card_w);
    g.close_y = settings_parts::popup_close_y();
  } else {
    g.close = settings_style::kEntryClose;
    g.close_x = card_w - settings_style::kEntryCloseRight - g.close;
    g.close_y = settings_style::kEntryCloseTop;
  }
  g.key_y = card_h - settings_style::kKeyboardBottom - (3 * settings_style::kKeyStep + settings_style::kKeyHeight);
#if !defined(DEVICE_LAYOUT_480X480)
  g.stacked = g.field_y + 2 * g.field_h + settings_style::kEntryFieldGap + popup_layout::scale(40) <= g.key_y;
  g.message_y = g.stacked ? g.field_y + 2 * g.field_h + popup_layout::scale(24) : g.field_y + g.field_h + popup_layout::scale(14);
#endif
  return g;
}

void focus_field(lv_obj_t* field) {
  if (!field || field == g_entry.focus) return;
  if (g_entry.focus) {
    lv_obj_remove_state(g_entry.focus, LV_STATE_FOCUSED);
    lv_obj_send_event(g_entry.focus, LV_EVENT_DEFOCUSED, nullptr);
  }
  g_entry.focus = field;
  lv_obj_add_state(field, LV_STATE_FOCUSED);
  // Starts the cursor's blinking.
  lv_obj_send_event(field, LV_EVENT_FOCUSED, nullptr);
}

void on_field_clicked(lv_event_t* e) {
  if (!g_entry.busy) focus_field(static_cast<lv_obj_t*>(lv_event_get_current_target(e)));
}

void on_eye_clicked(lv_event_t* e) {
  lv_obj_t* eye = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
  lv_obj_t* field = lv_obj_get_parent(eye);
  const bool hidden = lv_textarea_get_password_mode(field);
  lv_textarea_set_password_mode(field, !hidden);
  lv_label_set_text(eye, getMdiChar(hidden ? "eye-off" : "eye").c_str());
}

// The Settings card on the 480 class types in the row font, everything else
// in the larger field font (mockup fldFs).
const lv_font_t* field_font() {
#if defined(DEVICE_LAYOUT_480X480)
  if (!g_entry.setup) return settings_style::row_font();
#endif
  return popup_layout::font28();
}

// A round field (mockup .fld): the control color, the accent edge while it
// takes the keys, the placeholder grey, a blinking cursor.
lv_obj_t* field(int x, int y, int w, int h, const char* placeholder, bool secret, uint32_t max_length) {
  const Colors palette = settings_style::colors(g_entry.card_color);
  const lv_font_t* font = field_font();
  lv_obj_t* f = lv_textarea_create(g_entry.root);
  lv_obj_remove_style_all(f);
  lv_obj_set_pos(f, x, y);
  lv_obj_set_size(f, w, h);
  lv_textarea_set_one_line(f, true);
  lv_textarea_set_max_length(f, max_length);
  lv_textarea_set_placeholder_text(f, placeholder);
  lv_obj_set_style_bg_color(f, lv_color_hex(palette.group), 0);
  lv_obj_set_style_bg_opa(f, LV_OPA_COVER, 0);
  settings_style::apply_radius(f, h / 2);
  lv_obj_set_style_border_width(f, 2, 0);
  lv_obj_set_style_border_opa(f, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_color(f, lv_color_hex(settings_style::kWifiColor), LV_STATE_FOCUSED);
  lv_obj_set_style_border_opa(f, LV_OPA_COVER, LV_STATE_FOCUSED);
  lv_obj_set_style_text_font(f, font, 0);
  lv_obj_set_style_text_color(f, lv_color_white(), 0);
  lv_obj_set_style_text_color(f, lv_color_white(), LV_PART_TEXTAREA_PLACEHOLDER);
  lv_obj_set_style_text_opa(f, settings_style::kGreyOpa, LV_PART_TEXTAREA_PLACEHOLDER);
  const int pad_v = (h - 4 - lv_font_get_line_height(font)) / 2;
  lv_obj_set_style_pad_top(f, pad_v, 0);
  lv_obj_set_style_pad_bottom(f, pad_v, 0);
  lv_obj_set_style_pad_left(f, h * 4 / 10, 0);
  lv_obj_set_style_pad_right(f, h / 3, 0);
  lv_obj_set_scrollbar_mode(f, LV_SCROLLBAR_MODE_OFF);
  // The cursor: a 2 px line in the text color, blinking.
  lv_obj_set_style_border_color(f, lv_color_white(), LV_PART_CURSOR);
  lv_obj_set_style_border_width(f, 2, LV_PART_CURSOR);
  lv_obj_set_style_border_side(f, LV_BORDER_SIDE_LEFT, LV_PART_CURSOR);
  // Only in the field that takes the keys.
  lv_obj_set_style_border_opa(f, LV_OPA_TRANSP, LV_PART_CURSOR);
  lv_obj_set_style_border_opa(f, LV_OPA_COVER, LV_PART_CURSOR | LV_STATE_FOCUSED);
  lv_obj_set_style_anim_duration(f, 500, LV_PART_CURSOR);
  lv_obj_add_event_cb(f, on_field_clicked, LV_EVENT_CLICKED, nullptr);
  if (secret) {
    lv_textarea_set_password_mode(f, true);
    lv_obj_t* eye = lv_label_create(f);
    lv_label_set_text(eye, getMdiChar("eye").c_str());
    if (FONT_MDI_ICONS) lv_obj_set_style_text_font(eye, FONT_MDI_ICONS, 0);
    lv_obj_set_style_text_color(eye, lv_color_white(), 0);
    lv_obj_set_style_text_opa(eye, settings_style::kGreyOpa, 0);
    lv_obj_add_flag(eye, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(eye, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(eye, h / 4);
    lv_obj_add_event_cb(eye, on_eye_clicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_set_style_pad_right(f, h / 3 + settings_parts::icon_width() + h / 6, 0);
    lv_obj_align(eye, LV_ALIGN_RIGHT_MID, settings_parts::icon_width() + h / 6, 0);
  }
  return f;
}

void stop_dots() {
  if (g_entry.dots_timer) {
    lv_timer_delete(g_entry.dots_timer);
    g_entry.dots_timer = nullptr;
  }
  if (g_entry.dots) {
    lv_label_set_text(g_entry.dots, "");
    lv_obj_set_width(g_entry.dots, 0);
  }
}

void show_message(const char* text, uint32_t color) {
  if (!g_entry.message) return;
  stop_dots();
  lv_label_set_text(g_entry.message, text ? text : "");
  lv_obj_set_style_text_color(g_entry.message, lv_color_hex(color), 0);
  lv_obj_set_style_text_opa(g_entry.message, color == 0xFFFFFF ? settings_style::kGreyOpa : LV_OPA_COVER, 0);
}

void on_dots(lv_timer_t*) {
  if (!g_entry.dots) return;
  g_entry.dot_count = static_cast<uint8_t>((g_entry.dot_count + 1) % 4);
  static const char* const kDots[] = {"", ".", "..", "..."};
  lv_label_set_text(g_entry.dots, kDots[g_entry.dot_count]);
}

// "Connecting" with dots that keep moving, so a slow connection never looks
// stuck (user 2026-10-06). The text's own trailing dots become the moving ones.
void show_progress(const char* text) {
  if (!g_entry.message || !g_entry.dots || !text) return;
  show_message(text, 0xFFFFFF);
  char base[64];
  snprintf(base, sizeof(base), "%s", text);
  size_t length = strlen(base);
  while (length > 0 && base[length - 1] == '.') base[--length] = '\0';
  lv_label_set_text(g_entry.message, base);
  lv_obj_set_width(g_entry.dots, settings_parts::text_width(settings_style::small_font(), "..."));
  g_entry.dot_count = 3;
  lv_label_set_text(g_entry.dots, "...");
  g_entry.dots_timer = lv_timer_create(on_dots, 400, nullptr);
}

void on_key_text(const char* text) {
  if (g_entry.focus && !g_entry.busy) {
    lv_textarea_add_text(g_entry.focus, text);
    show_message("", 0xFFFFFF);
  }
}

void on_key_backspace() {
  if (g_entry.focus && !g_entry.busy) lv_textarea_delete_char(g_entry.focus);
}

// The keyboard's check connects (no extra Connect button).
void on_key_ok() {
  if (g_entry.busy) return;
  const char* ssid = g_entry.manual ? lv_textarea_get_text(g_entry.name) : g_entry.ssid;
  const char* password = lv_textarea_get_text(g_entry.password);
  if (!ssid || !ssid[0]) {
    focus_field(g_entry.name);
    return;
  }
  // A network found with a lock needs its password.
  if (!g_entry.manual && (!password || !password[0])) {
    focus_field(g_entry.password);
    return;
  }
  settings_model::wifi_connect(ssid, password);
  g_entry.busy = true;
  settings_keyboard::set_enabled(g_entry.keyboard, false);
  show_progress(settings_model::text().settings_connecting);
}

void on_close(lv_event_t*) {
  // While connecting the entry stays (mockup sheetClose).
  if (g_entry.busy) return;
  void (*closed)() = g_entry.closed;
  close(true);
  if (closed) closed();
}

settings_keyboard::Accents keyboard_accents() {
  switch (settings_model::keyboard_accents()) {
    case 1:
      return settings_keyboard::Accents::French;
    case 2:
      return settings_keyboard::Accents::Polish;
    default:
      return settings_keyboard::Accents::European;
  }
}

settings_keyboard::Layout keyboard_layout() {
  switch (settings_model::keyboard_layout()) {
    case 1:
      return settings_keyboard::Layout::Qwertz;
    case 2:
      return settings_keyboard::Layout::Azerty;
    default:
      return settings_keyboard::Layout::Qwerty;
  }
}

}  // namespace

// The title says what is asked (a found network's name where the fields do
// not repeat it), then the X, the fields, a line for "Connecting..." or an
// error, and the keyboard at the card's bottom.
void open(const Spec& spec, bool manual, const char* ssid) {
  if (!spec.card) return;
  close();
  settings_parts::close_options();
  const i18n::Strings& s = settings_model::text();
  lv_obj_update_layout(spec.card);
  const int card_w = lv_obj_get_width(spec.card);
  const int card_h = lv_obj_get_height(spec.card);
  const bool setup = spec.step_line != nullptr;
  const Geometry g = geometry(card_w, card_h, setup);
  g_entry.manual = manual;
  g_entry.setup = setup;
  g_entry.card_color = spec.card_color & 0xFFFFFF;
  g_entry.closed = spec.closed;
  snprintf(g_entry.ssid, sizeof(g_entry.ssid), "%s", ssid ? ssid : "");
  const Colors palette = settings_style::colors(g_entry.card_color);
  g_entry.root = settings_parts::plain(spec.card);
  lv_obj_set_size(g_entry.root, card_w, card_h);

  const char* title = manual ? s.settings_add_network : g.stacked ? s.settings_join_network : g_entry.ssid;
  if (setup) {
    // The setup's step head with the WiFi circle (mockup sheet(): wifi-lock).
    settings_parts::step_head(g_entry.root, card_w, "wifi-lock", settings_style::kWifiColor, g_entry.card_color, title,
                              spec.step_line, card_w - g.close_x + 10);
  } else {
    lv_obj_t* head = lv_label_create(g_entry.root);
    lv_label_set_text(head, title);
    lv_label_set_long_mode(head, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(head, lv_color_white(), 0);
    if (g.compact) {
      lv_obj_set_style_text_font(head, settings_style::row_font(), 0);
      lv_obj_set_width(head, g.close_x - g.pad - 8);
      lv_obj_set_pos(head, g.pad,
                     settings_parts::browser_label_y(settings_style::row_font(), settings_style::kRowFontPx, g.close_y,
                                                     g.close));
    } else {
      lv_obj_set_style_text_font(head, settings_style::page_title_font(), 0);
      lv_obj_set_width(head, g.close_x - g.pad - settings_style::kSectionLeft - 10);
      const int box = (settings_style::kTitleFontPx * 125 + 50) / 100;
      lv_obj_set_pos(head, g.pad + settings_style::kSectionLeft,
                     settings_parts::browser_label_y(settings_style::page_title_font(), settings_style::kTitleFontPx,
                                                     g.close_y + g.close / 2.0f - box / 2.0f, box));
    }
  }
  settings_parts::close_button(g_entry.root, g.close_x, g.close_y, g.close, on_close, nullptr);

  const int gap = settings_style::kKeyGap;
  if (g.stacked) {
    // Name above password over the keyboard's width; a found network's name
    // stands fixed in the first field.
    if (manual) {
      g_entry.name = field(g.pad, g.field_y, g.column, g.field_h, s.settings_network_name, false, 32);
    } else {
      lv_obj_t* fixed = settings_parts::plain(g_entry.root);
      lv_obj_set_pos(fixed, g.pad, g.field_y);
      lv_obj_set_size(fixed, g.column, g.field_h);
      settings_style::apply_radius(fixed, g.field_h / 2);
      lv_obj_set_style_border_width(fixed, 1, 0);
      lv_obj_set_style_border_color(fixed, lv_color_white(), 0);
      lv_obj_set_style_border_opa(fixed, 51, 0);
      lv_obj_set_style_pad_left(fixed, g.field_h * 4 / 10, 0);
      lv_obj_set_style_pad_right(fixed, g.field_h / 3, 0);
      lv_obj_set_flex_flow(fixed, LV_FLEX_FLOW_ROW);
      lv_obj_set_flex_align(fixed, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
      lv_obj_t* name = lv_label_create(fixed);
      lv_label_set_text(name, g_entry.ssid);
      lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
      lv_obj_set_width(name, 1);
      lv_obj_set_flex_grow(name, 1);
      lv_obj_set_style_text_font(name, field_font(), 0);
      lv_obj_set_style_text_color(name, lv_color_white(), 0);
      settings_parts::trailing_icon(fixed, "lock-outline");
    }
    g_entry.password = field(g.pad, g.field_y + g.field_h + settings_style::kEntryFieldGap, g.column, g.field_h,
                             s.wifi_password_label, true, 63);
  } else if (manual) {
    // Name and password side by side.
    const int half = (g.column - 2 * gap) / 2;
    g_entry.name = field(g.pad, g.field_y, half, g.field_h, s.settings_network_name, false, 32);
    g_entry.password = field(g.pad + half + 2 * gap, g.field_y, half, g.field_h, s.wifi_password_label, true, 63);
  } else {
    g_entry.password = field(g.pad, g.field_y, g.column, g.field_h, s.wifi_password_label, true, 63);
  }
  // The saved password of the saved network, so it does not look lost.
  if (!manual) lv_textarea_set_text(g_entry.password, settings_model::wifi_saved_password(g_entry.ssid));

  // The message and the moving dots, centered together.
  lv_obj_t* line = settings_parts::plain(g_entry.root);
  lv_obj_set_pos(line, g.pad, g.message_y);
  lv_obj_set_size(line, g.column, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(line, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(line, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  g_entry.message = lv_label_create(line);
  lv_label_set_text(g_entry.message, "");
  lv_label_set_long_mode(g_entry.message, LV_LABEL_LONG_DOT);
  lv_obj_set_width(g_entry.message, LV_SIZE_CONTENT);
  lv_obj_set_style_max_width(g_entry.message, g.column, 0);
  lv_obj_set_style_text_font(g_entry.message, settings_style::small_font(), 0);
  g_entry.dots = lv_label_create(line);
  lv_label_set_text(g_entry.dots, "");
  lv_label_set_long_mode(g_entry.dots, LV_LABEL_LONG_CLIP);
  lv_obj_set_width(g_entry.dots, 0);
  lv_obj_set_style_text_font(g_entry.dots, settings_style::small_font(), 0);
  lv_obj_set_style_text_color(g_entry.dots, lv_color_white(), 0);
  lv_obj_set_style_text_opa(g_entry.dots, settings_style::kGreyOpa, 0);

  const settings_keyboard::Geometry keys = {g.pad,
                                            g.key_y,
                                            g.column,
                                            settings_style::kKeyHeight,
                                            settings_style::kKeyStep,
                                            gap,
                                            settings_style::kKeyRadius};
  g_entry.keyboard = settings_keyboard::create(g_entry.root, keys, keyboard_layout(), keyboard_accents(), palette,
                                               settings_style::kWifiColor,
                                               {on_key_text, on_key_backspace, on_key_ok});
  focus_field(manual ? g_entry.name : g_entry.password);
}

void close(bool from_event) {
  if (!g_entry.root) return;
  lv_obj_t* root = g_entry.root;
  stop_dots();
  g_entry = Entry();
  settings_model::wifi_connect_done();
  if (from_event) {
    lv_obj_delete_async(root);
  } else {
    lv_obj_delete(root);
  }
}

bool is_open() { return g_entry.root != nullptr; }

bool tick(const settings_model::WifiValues& v) {
  if (!g_entry.root || !g_entry.busy || v.connecting) return false;
  if (v.connect_failed) {
    // Wrong password or out of reach: edit and try again.
    g_entry.busy = false;
    settings_keyboard::set_enabled(g_entry.keyboard, true);
    show_message(settings_model::text().settings_connect_failed, settings_style::kErrorColor);
    return false;
  }
  close();
  return true;
}

}  // namespace settings_entry
