#include "src/ui/tabs/settings/setup_screen.h"

#include <cstdio>
#include <cstring>

#include "src/tiles/icons/mdi_icons.h"
#include "src/ui/tabs/settings/settings_entry.h"
#include "src/ui/tabs/settings/settings_model.h"
#include "src/ui/tabs/settings/settings_parts.h"
#include "src/ui/tabs/settings/settings_screen.h"
#include "src/ui/tabs/settings/settings_style.h"

// The setup runs in the popup card (popup_layout: centered, square on
// landscape panels, the full height on portrait ones) on the Settings panel,
// whose frame is gone meanwhile: the step's circle, title and "Step n of 4"
// top left, the X top right, the step's body, Back and Next in the bottom
// corners with the step dots between them (mockup wzCard, wzBody, wzFoot).
// One task per step, almost no text; each step waits for Next (user
// 2026-10-06: step by step, no jump after WiFi or the pairing).
namespace setup_screen {
namespace {

using settings_model::LocaleList;
using settings_model::PairState;
using settings_style::Colors;

constexpr uint8_t kStepCount = 4;
// The steps' circles (mockup WSTEPS) and the setup's accent (WZ_ACC).
struct StepLook {
  const char* icon;
  uint32_t color;
};
constexpr StepLook kSteps[kStepCount] = {{"translate", settings_style::kLocalizationColor},
                                         {"wifi", settings_style::kWifiColor},
                                         {"link-variant", 0x18BCF2},
                                         {"view-grid-plus-outline", 0x26A69A}};
constexpr uint32_t kAccent = 0x26A69A;
// The setup opens the pairing window again at most this often.
constexpr uint32_t kPairRetryMs = 3000;
// A search without a result is repeated after this long.
constexpr uint32_t kScanRetryMs = 5000;

enum class Action : uint8_t {
  Back,
  Next,
  Finish,
  Close,
  Language,
  TimeZone,
  Network,
  OtherNetwork,
  Search,
  Phone,
  ToWifi,
  Retry,
  Guide,
  Confirm,
  StayInSetup,
  Leave,
};

lv_obj_t* g_panel = nullptr;
lv_obj_t* g_card = nullptr;
// Head, body and foot; hidden while the WiFi entry fills the card.
lv_obj_t* g_content = nullptr;
lv_obj_t* g_body = nullptr;
lv_obj_t* g_foot = nullptr;
lv_obj_t* g_rotation = nullptr;
lv_obj_t* g_spinner = nullptr;
lv_obj_t* g_dialog = nullptr;
struct LocaleRow {
  lv_obj_t* row;
  lv_obj_t* value;
  lv_obj_t* chevron;
};
LocaleRow g_locale_rows[2] = {};
lv_timer_t* g_timer = nullptr;
lv_timer_t* g_spin_timer = nullptr;
// What the card was built for: another step, language or tile color builds
// it again.
uint8_t g_step = 0;
uint32_t g_built_card = 0;
const i18n::Strings* g_built_text = nullptr;
// The body's state; a change builds the body and the foot again.
uint32_t g_key = 0;
// Step 3: the Bridge guide shows instead of the pairing steps.
bool g_guide = false;
uint32_t g_pair_tried_at = 0;
uint32_t g_scan_at = 0;
int g_spin_angle = 0;
// The networks listed (a tap names one by its index).
settings_model::WifiNetwork g_networks[24] = {};
uint8_t g_network_count = 0;

Colors colors() { return settings_style::colors(g_built_card); }

void* data(Action action) { return reinterpret_cast<void*>(static_cast<uintptr_t>(action)); }

int card_width() { return popup_layout::kCardWidth; }
int card_height() { return popup_layout::kCardHeight; }
int body_top() {
  return popup_layout::kHeaderCenterY + popup_layout::kHeaderIconDiscSize / 2 + popup_layout::kHeaderIconDiscGap;
}
int body_width() { return card_width() - 2 * popup_layout::kCardPad; }
int body_height() { return card_height() - body_top() - settings_style::kSetupFoot; }

const char* step_title(uint8_t step) {
  const i18n::Strings& s = settings_model::text();
  switch (step) {
    case 0:
      return s.setup_language_title;
    case 1:
      return s.setup_wifi_title;
    case 2:
      return s.setup_ha_title;
    default:
      return s.setup_tiles_title;
  }
}

void step_line(char* buf, size_t len) {
  snprintf(buf, len, settings_model::text().setup_step_fmt, g_step + 1, kStepCount);
}

bool wifi_done(const settings_model::WifiValues& v) { return v.connected && !v.access_point; }

bool step_done(uint8_t step) {
  if (step == 1) return wifi_done(settings_model::wifi_values());
  if (step == 2) return settings_model::system_values().pairing == PairState::Paired;
  return false;
}

void color_text(lv_obj_t* label, uint32_t color) {
  if (!label) return;
  lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
  lv_obj_set_style_text_opa(label, LV_OPA_COVER, 0);
}

// ---------- Spinner ----------
// The waiting rows' icon turns (mockup .spin): one turn a second in steps,
// so only its small area is drawn now and then.

void on_spin(lv_timer_t*) {
  if (!g_spinner) return;
  g_spin_angle = (g_spin_angle + 180) % 3600;
  lv_obj_set_style_transform_rotation(g_spinner, g_spin_angle, 0);
}

void stop_spinner() {
  g_spinner = nullptr;
  if (g_spin_timer) {
    lv_timer_delete(g_spin_timer);
    g_spin_timer = nullptr;
  }
}

// A row whose icon is the turning circle.
settings_parts::Row spinner_row(lv_obj_t* group, const char* title, const char* sub) {
  settings_parts::Row r = settings_parts::row(group, "loading", title, sub);
  g_spinner = r.icon;
  lv_obj_set_style_transform_pivot_x(g_spinner, lv_pct(50), 0);
  lv_obj_set_style_transform_pivot_y(g_spinner, lv_pct(50), 0);
  if (!g_spin_timer) g_spin_timer = lv_timer_create(on_spin, 50, nullptr);
  return r;
}

// ---------- Actions ----------

void build_card();
void build_body();
void tick();
void open_leave_dialog();
void close_dialog(bool from_event = false);

void go_to(uint8_t step) {
  if (step >= kStepCount) return;
  settings_model::setup_step_changed(step);
  g_guide = false;
  build_card();
}

// A touch on the card moves after its event (the card is built anew).
uint8_t g_wanted_step = 0;
void go_async(void*) { go_to(g_wanted_step); }
void go_later(uint8_t step) {
  g_wanted_step = step;
  lv_async_call(go_async, nullptr);
}

void go_home_async(void*) { settings_model::close_settings(); }

void finish() {
  settings_model::setup_end();
  // Home shows; switching tabs removes this card, the touched button with it.
  lv_async_call(go_home_async, nullptr);
}

void refresh_async(void*) { tick(); }
void rebuild_async(void*) { build_body(); }

void show_content(bool shown) {
  if (!g_content) return;
  if (shown) {
    lv_obj_remove_flag(g_content, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(g_content, LV_OBJ_FLAG_HIDDEN);
  }
}

// The entry's X: the step again, with a fresh search.
void on_entry_closed() {
  show_content(true);
  settings_model::wifi_scan();
  lv_async_call(rebuild_async, nullptr);
}

void open_entry(bool manual, const char* ssid) {
  char line[48];
  step_line(line, sizeof(line));
  show_content(false);
  settings_entry::open({g_card, g_built_card, line, on_entry_closed}, manual, ssid);
}

void open_locale_list(uint8_t index);

void set_chevron(uint8_t index, bool up) {
  if (index >= 2 || !g_locale_rows[index].chevron) return;
  lv_label_set_text(g_locale_rows[index].chevron, getMdiChar(up ? "chevron-up" : "chevron-down").c_str());
}

LocaleList locale_list(uint8_t index) { return index == 0 ? LocaleList::Language : LocaleList::TimeZone; }

const char* option_text(uint8_t tag, uint8_t index) { return settings_model::locale_option(locale_list(tag), index); }

// A tap on the other row opens its list right away (as on Localization).
void on_options_closed(uint8_t tag, const lv_point_t* tapped) {
  set_chevron(tag, false);
  if (!tapped) return;
  const uint8_t other = tag == 0 ? 1 : 0;
  if (!g_locale_rows[other].row) return;
  lv_area_t area;
  lv_obj_get_coords(g_locale_rows[other].row, &area);
  if (tapped->x >= area.x1 && tapped->x <= area.x2 && tapped->y >= area.y1 && tapped->y <= area.y2) {
    open_locale_list(other);
  }
}

// Saved at once; a new language builds the setup again in it.
void on_option_picked(uint8_t tag, uint8_t index) {
  const LocaleList list = locale_list(tag);
  if (index == settings_model::locale_selected(list)) return;
  settings_model::locale_selected_changed(list, index);
  if (tag < 2 && g_locale_rows[tag].value) {
    lv_label_set_text(g_locale_rows[tag].value, settings_screen::locale_value(list));
  }
}

void open_locale_list(uint8_t index) {
  if (index >= 2 || !g_locale_rows[index].row || !g_body) return;
  const LocaleList list = locale_list(index);
  settings_parts::OptionList spec = {};
  spec.host = g_panel;
  spec.row = g_locale_rows[index].row;
  lv_obj_update_layout(g_panel);
  lv_obj_get_coords(g_body, &spec.bounds);
  spec.tag = index;
  spec.count = settings_model::locale_option_count(list);
  spec.selected = settings_model::locale_selected(list);
  spec.card = g_built_card;
  spec.selected_color = settings_style::tone(g_built_card, settings_style::kLocalizationColor).disc;
  spec.text_x = settings_style::kRowPadLeft + settings_parts::icon_width() + settings_style::kRowGap;
  spec.handler = {option_text, on_option_picked, on_options_closed};
  settings_parts::open_options(spec);
  set_chevron(index, true);
}

void on_network(lv_event_t* e) {
  const uint8_t index = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  if (index >= g_network_count) return;
  const settings_model::WifiNetwork network = g_networks[index];
  if (network.locked) {
    open_entry(false, network.ssid);
    return;
  }
  // An open network connects right away.
  settings_model::wifi_connect(network.ssid, "");
  lv_async_call(refresh_async, nullptr);
}

void on_action(lv_event_t* e) {
  const auto action = static_cast<Action>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  switch (action) {
    case Action::Back:
      if (g_step > 0) go_later(g_step - 1);
      return;
    case Action::Next:
      go_later(g_step + 1);
      return;
    case Action::Finish:
      finish();
      return;
    case Action::Close:
      open_leave_dialog();
      return;
    case Action::Language:
      open_locale_list(0);
      return;
    case Action::TimeZone:
      open_locale_list(1);
      return;
    case Action::OtherNetwork:
      open_entry(true, nullptr);
      return;
    case Action::Search:
      settings_model::wifi_scan();
      break;
    case Action::Phone: {
      const bool on = settings_model::wifi_values().access_point;
      settings_model::hotspot_selected(!on);
      // Off again: search once the WiFi is back.
      if (on) settings_model::wifi_scan();
      break;
    }
    case Action::ToWifi:
      go_later(1);
      return;
    case Action::Retry:
      g_pair_tried_at = lv_tick_get() | 1;
      settings_model::pair();
      break;
    case Action::Guide:
      g_guide = !g_guide;
      break;
    case Action::Confirm:
      settings_model::confirm_pairing();
      break;
    case Action::StayInSetup:
      close_dialog(true);
      return;
    case Action::Leave:
      close_dialog(true);
      finish();
      return;
    case Action::Network:
      return;
  }
  // The body may be built again, the touched object with it: after the event.
  lv_async_call(refresh_async, nullptr);
}

// ---------- Leave dialog ----------

void close_dialog(bool from_event) {
  lv_obj_t* root = g_dialog;
  g_dialog = nullptr;
  if (!root) return;
  settings_parts::conceal(root, lv_obj_get_child(root, 0));
  if (from_event) {
    lv_obj_delete_async(root);
  } else {
    lv_obj_delete(root);
  }
}

void on_dialog_outside(lv_event_t*) { close_dialog(true); }

// "Leave the setup?" - it can be finished later in System > Setup.
void open_leave_dialog() {
  close_dialog();
  settings_parts::close_options();
  const i18n::Strings& s = settings_model::text();
  lv_obj_t* box = settings_parts::dialog(g_panel, g_built_card, s.setup_leave_question, on_dialog_outside);
  char text[200];
  snprintf(text, sizeof(text), s.setup_leave_text_fmt, s.tile_type_settings, s.settings_setup);
  settings_parts::dialog_text(box, text);
  lv_obj_t* buttons = settings_parts::dialog_buttons(box);
  struct Choice {
    const char* text;
    Action action;
  };
  for (const Choice& choice : {Choice{s.security_cancel, Action::StayInSetup}, Choice{s.setup_leave, Action::Leave}}) {
    lv_obj_t* button = settings_parts::button(buttons, choice.text, nullptr, settings_parts::ButtonKind::Normal,
                                              kAccent, colors(), settings_style::kDialogButtonHeight, true, on_action,
                                              data(choice.action));
    lv_obj_set_flex_grow(button, 1);
  }
  g_dialog = lv_obj_get_parent(box);
  settings_parts::reveal(g_dialog, box);
}

// ---------- Steps ----------

// 1: language, time zone (a new panel starts on Berlin) and rotation, with
// the rows of the Settings pages; simpler, but no option left out.
void build_language(const Colors& palette) {
  const bool stacked = card_width() < settings_style::kStackedCardWidth;
  const int value_width = (body_width() - settings_style::kRowPadLeft - settings_style::kRowPadRight) / 2;
  lv_obj_t* group = settings_parts::group(g_body, palette);
  for (uint8_t i = 0; i < 2; ++i) {
    const LocaleList list = locale_list(i);
    LocaleRow& view = g_locale_rows[i];
    const char* value = settings_screen::locale_value(list);
    settings_parts::Row row = settings_parts::row(group, settings_screen::locale_icon(list),
                                                  settings_screen::locale_title(list), stacked ? value : nullptr);
    view.row = row.row;
    view.value = stacked ? row.sub : settings_parts::trailing_text(row.row, value, value_width);
    view.chevron = settings_parts::trailing_icon(row.row, "chevron-down");
    settings_parts::make_tap(row.row, palette.button, on_action, data(i == 0 ? Action::Language : Action::TimeZone));
  }
  settings_parts::Row rotation =
      settings_parts::row(group, "screen-rotation", settings_model::text().settings_rotation);
  g_rotation = settings_screen::rotation_segment(rotation.row, palette.card);
}

void signal_icon(char* buf, size_t len, uint8_t bars, bool locked) {
  if (bars == 0) {
    snprintf(buf, len, "%s", locked ? "wifi-strength-lock-outline" : "wifi-strength-outline");
  } else {
    snprintf(buf, len, "wifi-strength-%u%s", bars, locked ? "-lock" : "");
  }
}

// A QR code beside a title and a grey line (mockup wzQrRow).
void qr_row(lv_obj_t* group, const char* code, const char* title, const char* line) {
  settings_parts::Row row = settings_parts::row(group, nullptr, title, line);
  lv_obj_set_height(row.row, settings_style::kHotspotQr + 2 * settings_style::kHotspotQrPad);
  lv_obj_t* qr = settings_parts::qr_code(row.row, settings_style::kHotspotQr, code);
  if (qr) lv_obj_move_to_index(qr, lv_obj_get_index(row.text));
}

// "Set up with your phone": the hotspot runs only while it is switched on,
// so the search works the rest of the time.
void phone_row(lv_obj_t* group, const settings_model::WifiValues& v, const Colors& palette) {
  settings_parts::Row row =
      settings_parts::row(group, "cellphone-wireless", settings_model::text().setup_with_phone);
  settings_parts::toggle(row.row, v.access_point, kAccent, palette.card, !v.hotspot_switching, on_action,
                         data(Action::Phone));
}

// 2: every network in a box that scrolls by itself; Other network and the
// phone setup always visible below it.
void build_wifi(const Colors& palette) {
  const i18n::Strings& s = settings_model::text();
  const settings_model::WifiValues v = settings_model::wifi_values();
  g_network_count = 0;
  if (wifi_done(v)) {
    char name[40];
    char icon[32];
    char address[48];
    char rest[64] = "";
    settings_model::network_name(name, sizeof(name));
    if (v.ethernet_active) {
      snprintf(icon, sizeof(icon), "ethernet");
    } else {
      signal_icon(icon, sizeof(icon), v.bars, false);
    }
    lv_obj_t* group = settings_parts::group(g_body, palette);
    settings_parts::Row row = settings_parts::row(group, icon, name, "");
    if (settings_model::panel_address(address, sizeof(address))) snprintf(rest, sizeof(rest), " \xC2\xB7 %s", address);
    settings_parts::two_tone_sub(row, s.wifi_connected, settings_style::kGoodColor, rest);
    color_text(settings_parts::trailing_icon(row.row, "check"), settings_style::kGoodColor);
    return;
  }
  if (v.access_point) {
    lv_obj_t* group = settings_parts::group(g_body, palette);
    phone_row(group, v, palette);
    char ssid[40];
    char password[72];
    char join[128];
    char line[96];
    settings_model::hotspot_details(ssid, sizeof(ssid), password, sizeof(password));
    snprintf(join, sizeof(join), "WIFI:T:WPA;S:%s;P:%s;;", ssid, password);
    snprintf(line, sizeof(line), "%s %s", s.wifi_password_label, password);
    qr_row(group, join, ssid, line);
    return;
  }
  // Networks and Search, as on the WiFi page (user 2026-10-06).
  settings_screen::networks_heading(g_body, v.scanning, true, on_action, data(Action::Search));
  lv_obj_t* list = settings_parts::group(g_body, palette);
  if (v.connecting) {
    spinner_row(list, s.settings_connecting, nullptr);
  } else {
    g_network_count = settings_model::wifi_network_count();
    if (g_network_count > 24) g_network_count = 24;
    for (uint8_t i = 0; i < g_network_count; ++i) {
      g_networks[i] = settings_model::wifi_network(i);
      char icon[32];
      signal_icon(icon, sizeof(icon), g_networks[i].bars, g_networks[i].locked);
      settings_parts::Row row = settings_parts::row(list, icon, g_networks[i].ssid);
      settings_parts::trailing_icon(row.row, "chevron-right");
      settings_parts::make_tap(row.row, palette.button, on_network, reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
    }
  }
  lv_obj_t* more = settings_parts::group(g_body, palette);
  lv_obj_set_style_margin_top(more, settings_style::kSectionBottom, 0);
  settings_parts::Row other = settings_parts::row(more, "plus", s.setup_other_network);
  settings_parts::trailing_icon(other.row, "chevron-right");
  settings_parts::make_tap(other.row, palette.button, on_action, data(Action::OtherNetwork));
  phone_row(more, v, palette);

  // The list keeps what the body leaves under the heading and above the
  // second group, and scrolls in itself.
  lv_obj_update_layout(g_body);
  const int room = body_height() - lv_obj_get_y(list) - lv_obj_get_height(more) - settings_style::kSectionBottom;
  if (lv_obj_get_height(list) > room && room > settings_style::kRowHeight) {
    lv_obj_set_height(list, room);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
  }
}

// A label in a column of the body: centered, wrapping.
lv_obj_t* column_text(lv_obj_t* parent, const char* text, const lv_font_t* font, int px, bool grey, int top,
                      bool centered) {
  lv_obj_t* label = lv_label_create(parent);
  lv_label_set_text(label, text ? text : "");
  lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(label, LV_PCT(100));
  if (centered) lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, lv_color_white(), 0);
  if (grey) lv_obj_set_style_text_opa(label, settings_style::kGreyOpa, 0);
  settings_parts::browser_line(label, px, top);
  return label;
}

// A step of the Home Assistant instructions: its number in a filled circle
// (easier to read than an outline digit), what to do and where.
void number_row(lv_obj_t* group, int number, const char* title, const char* sub) {
  settings_parts::Row row = settings_parts::row(group, nullptr, title, sub);
  lv_obj_t* circle = settings_parts::plain(row.row);
  lv_obj_set_size(circle, settings_style::kIconPx, settings_style::kIconPx);
  lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(circle, lv_color_hex(kAccent), 0);
  lv_obj_set_style_bg_opa(circle, LV_OPA_COVER, 0);
  lv_obj_move_to_index(circle, lv_obj_get_index(row.text));
  lv_obj_t* digit = lv_label_create(circle);
  char text[4];
  snprintf(text, sizeof(text), "%d", number);
  lv_label_set_text(digit, text);
  lv_obj_set_style_text_font(digit, settings_style::row_font(), 0);
  lv_obj_set_style_text_color(digit, lv_color_hex(settings_style::kAccentText), 0);
  lv_obj_center(digit);
}

// "No HomeTiles Bridge yet?": opens the guide's QR code (or closes it).
void guide_row(lv_obj_t* group, const Colors& palette) {
  const i18n::Strings& s = settings_model::text();
  settings_parts::Row row =
      settings_parts::row(group, "book-open-variant", s.setup_no_bridge, g_guide ? s.setup_back_to_pairing : s.setup_guide);
  settings_parts::trailing_icon(row.row, g_guide ? "chevron-up" : "qrcode");
  settings_parts::make_tap(row.row, palette.button, on_action, data(Action::Guide));
}

// The address without its scheme and the closing slash, as people type it.
void shown_url(const char* url, char* buf, size_t len) {
  const char* start = strstr(url, "://") ? strstr(url, "://") + 3 : url;
  snprintf(buf, len, "%s", start);
  const size_t n = strlen(buf);
  if (n > 0 && buf[n - 1] == '/') buf[n - 1] = '\0';
}

// The pairing number: both sides confirm it (firmware pairing_compare).
void build_code(const settings_model::SystemValues& v, const Colors& palette) {
  const i18n::Strings& s = settings_model::text();
  lv_obj_t* box = settings_parts::group(g_body, palette);
  lv_obj_set_style_pad_top(box, settings_style::kSetupCodePadTop, 0);
  lv_obj_set_style_pad_bottom(box, settings_style::kSetupCodePadTop, 0);
  lv_obj_set_style_pad_hor(box, settings_style::kSetupCodePadSide, 0);
  lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  column_text(box, s.setup_pairing_code, settings_style::small_font(), settings_style::kSmallFontPx, true, 0, true);
  char number[24] = "";
  settings_model::pairing_number(number, sizeof(number));
#if defined(DEVICE_LAYOUT_480X480)
  const lv_font_t* code_font = popup_layout::font72();
#else
  const lv_font_t* code_font = popup_layout::font80();
#endif
  lv_obj_t* code = lv_label_create(box);
  lv_label_set_text(code, number);
  lv_obj_set_style_text_font(code, code_font, 0);
  lv_obj_set_style_text_color(code, lv_color_white(), 0);
  lv_obj_set_style_margin_top(code, settings_style::kSetupCodeGap, 0);
  lv_obj_set_style_margin_bottom(code, settings_style::kSetupCodeGap, 0);
  const bool confirmed = v.pairing == PairState::Confirmed;
  column_text(box, confirmed ? s.pairing_waiting : s.pairing_compare, settings_style::row_font(),
              settings_style::kRowFontPx, false, 0, true);
  lv_obj_t* confirm = settings_parts::button(box, s.security_confirm, "check", settings_parts::ButtonKind::Accent,
                                             kAccent, palette, settings_style::kButtonHeight, false, on_action,
                                             data(Action::Confirm));
  lv_obj_set_style_margin_top(confirm, settings_style::kSetupCodeButtonTop, 0);
  settings_parts::button_set_enabled(confirm, !confirmed);
}

// 3: Open Home Assistant, Add HomeTiles, then wait; the number to confirm
// once Home Assistant pairs, "Later" until then.
void build_home_assistant(const Colors& palette) {
  const i18n::Strings& s = settings_model::text();
  if (!wifi_done(settings_model::wifi_values())) {
    lv_obj_t* group = settings_parts::group(g_body, palette);
    settings_parts::Row row = settings_parts::row(group, "wifi-off", s.setup_wifi_first);
    color_text(row.icon, settings_style::kWarnColor);
    settings_parts::trailing_icon(row.row, "chevron-right");
    settings_parts::make_tap(row.row, palette.button, on_action, data(Action::ToWifi));
    return;
  }
  const settings_model::SystemValues v = settings_model::system_values();
  if (v.pairing == PairState::Paired) {
    lv_obj_t* group = settings_parts::group(g_body, palette);
    settings_parts::Row row = settings_parts::row(group, "shield-check", s.setup_paired);
    color_text(row.icon, settings_style::kGoodColor);
    return;
  }
  if (v.pairing == PairState::Compare || v.pairing == PairState::Confirmed) {
    build_code(v, palette);
    return;
  }
  if (g_guide) {
    lv_obj_t* group = settings_parts::group(g_body, palette);
    char shown[64];
    shown_url(settings_model::setup_guide_url(), shown, sizeof(shown));
    qr_row(group, settings_model::setup_guide_url(), s.setup_install_bridge, shown);
    guide_row(group, palette);
    return;
  }
  lv_obj_t* steps = settings_parts::group(g_body, palette);
  char discovered[96];
  snprintf(discovered, sizeof(discovered), s.setup_discovered_fmt, settings_model::device_name());
  number_row(steps, 1, s.setup_open_ha, s.setup_open_ha_where);
  number_row(steps, 2, s.setup_add_hometiles, discovered);

  lv_obj_t* wait = settings_parts::group(g_body, palette);
  lv_obj_set_style_margin_top(wait, settings_style::kSectionTop, 0);
  const char* failure = nullptr;
  uint32_t failure_color = settings_style::kWarnColor;
  switch (v.pairing) {
    case PairState::NoAnswer:
      failure = s.pairing_no_answer;
      break;
    case PairState::AlreadyPaired:
      failure = s.pairing_already_paired;
      break;
    case PairState::Busy:
      failure = s.pairing_busy;
      break;
    case PairState::Rejected:
      failure = s.pairing_rejected;
      failure_color = settings_style::kErrorColor;
      break;
    case PairState::Failed:
      failure = s.pairing_failed;
      failure_color = settings_style::kErrorColor;
      break;
    default:
      break;
  }
  if (failure) {
    // A finished attempt waits for a tap, so a refusal is not repeated.
    settings_parts::Row row = settings_parts::row(wait, "refresh", failure, s.setup_tap_retry);
    color_text(row.title, failure_color);
    settings_parts::make_tap(row.row, palette.button, on_action, data(Action::Retry));
  } else {
    spinner_row(wait, v.pairing == PairState::Asking ? s.pairing_asking : s.pairing_waiting, s.setup_continues);
  }
  guide_row(wait, palette);
}

// 4: the Web Admin QR code, large, and the password hint; beside the text
// only where the card is wide enough for both.
void build_tiles(const Colors& palette) {
  const i18n::Strings& s = settings_model::text();
  char url[64];
  if (!settings_model::web_admin_url(url, sizeof(url))) {
    lv_obj_t* group = settings_parts::group(g_body, palette);
    settings_parts::Row row = settings_parts::row(group, "wifi-off", s.setup_wifi_first);
    color_text(row.icon, settings_style::kWarnColor);
    settings_parts::trailing_icon(row.row, "chevron-right");
    settings_parts::make_tap(row.row, palette.button, on_action, data(Action::ToWifi));
    return;
  }
  const int avail = body_height();
#if defined(DEVICE_LAYOUT_480X480)
  const bool wide = false;
  int qr = avail - 150 < 210 ? avail - 150 : 210;
#else
  const bool wide = body_width() >= settings_style::kSetupWideTiles;
  int qr = avail - popup_layout::scale(200) < popup_layout::scale(300) ? avail - popup_layout::scale(200)
                                                                       : popup_layout::scale(300);
  if (wide) qr = avail - 48 < 320 ? avail - 48 : 320;
#endif
  lv_obj_t* holder = nullptr;
  lv_obj_t* text = nullptr;
  if (wide) {
    holder = settings_parts::group(g_body, palette);
    lv_obj_set_height(holder, avail < qr + 48 ? avail : qr + 48);
    lv_obj_set_flex_flow(holder, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(holder, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(holder, 32, 0);
    lv_obj_set_style_pad_column(holder, 40, 0);
    settings_parts::qr_code(holder, qr, url);
    text = settings_parts::plain(holder);
    lv_obj_set_size(text, 1, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(text, 1);
  } else {
    holder = settings_parts::plain(g_body);
    lv_obj_set_size(holder, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(holder, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(holder, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(holder, 4, 0);
    settings_parts::qr_code(holder, qr, url);
    text = settings_parts::plain(holder);
    lv_obj_set_size(text, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_margin_top(text, 10, 0);
  }
  lv_obj_set_flex_flow(text, LV_FLEX_FLOW_COLUMN);
  lv_obj_add_flag(text, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  const bool centered = !wide;
  // "Web Admin" in font28 (28 / 24 / 20 px).
#if defined(DEVICE_LAYOUT_480X480)
  constexpr int kNamePx = 20;
  const int scan_top = 10;
#elif defined(DEVICE_LAYOUT_1024X600)
  constexpr int kNamePx = 24;
  const int scan_top = wide ? 24 : popup_layout::scale(14);
#else
  constexpr int kNamePx = 28;
  const int scan_top = wide ? 24 : popup_layout::scale(14);
#endif
  column_text(text, settings_style::kWebAdmin, popup_layout::font28(), kNamePx, false, 0, centered);
  column_text(text, url, settings_style::small_font(), settings_style::kSmallFontPx, true, 4, centered);
  column_text(text, s.setup_scan_tiles, settings_style::row_font(), settings_style::kRowFontPx, false, scan_top,
              centered);
  column_text(text, wide ? s.setup_password_hint_long : s.setup_password_hint, settings_style::small_font(),
              settings_style::kSmallFontPx, true, wide ? 16 : 8, centered);
}

// Back bottom left, the step dots in the middle, Next (or Later, Finish)
// bottom right; no Next while the step waits for something that moves on by
// itself.
void build_foot(const Colors& palette) {
  const i18n::Strings& s = settings_model::text();
  const int pad = popup_layout::kCardPad;
  const int height = settings_style::kButtonHeight;
  g_foot = settings_parts::plain(g_content);
  lv_obj_set_size(g_foot, card_width() - 2 * pad, height);
  lv_obj_set_pos(g_foot, pad, card_height() - pad - height);
  lv_obj_add_flag(g_foot, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  if (g_step > 0) {
    lv_obj_t* back = settings_parts::button(g_foot, s.wifi_back_btn, "chevron-left", settings_parts::ButtonKind::Normal,
                                            kAccent, palette, height, false, on_action, data(Action::Back));
    lv_obj_align(back, LV_ALIGN_LEFT_MID, 0, 0);
  }
  lv_obj_t* dots = settings_parts::plain(g_foot);
  lv_obj_set_size(dots, LV_SIZE_CONTENT, settings_style::kSetupDot);
  lv_obj_set_flex_flow(dots, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(dots, settings_style::kSetupDot, 0);
  for (uint8_t i = 0; i < kStepCount; ++i) {
    lv_obj_t* dot = settings_parts::plain(dots);
    lv_obj_set_size(dot, i == g_step ? settings_style::kSetupDotCurrent : settings_style::kSetupDot,
                    settings_style::kSetupDot);
    lv_obj_set_style_radius(dot, settings_style::kSetupDot / 2, 0);
    lv_obj_set_style_bg_color(dot, i == g_step ? lv_color_hex(kAccent) : lv_color_white(), 0);
    lv_obj_set_style_bg_opa(dot, i == g_step ? LV_OPA_COVER : LV_OPA_20, 0);
  }
  lv_obj_align(dots, LV_ALIGN_CENTER, 0, 0);

  const char* label = s.setup_next;
  Action action = Action::Next;
  settings_parts::ButtonKind kind = settings_parts::ButtonKind::Accent;
  if (g_step == 3) {
    label = s.setup_finish;
    action = Action::Finish;
  } else if (g_step == 1 && !step_done(1)) {
    label = nullptr;
  } else if (g_step == 2 && !step_done(2)) {
    label = s.setup_later;
    kind = settings_parts::ButtonKind::Normal;
  }
  if (label) {
    lv_obj_t* next =
        settings_parts::button(g_foot, label, nullptr, kind, kAccent, palette, height, false, on_action, data(action));
    lv_obj_align(next, LV_ALIGN_RIGHT_MID, 0, 0);
  }
}

// What the body shows apart from countdowns: a change builds it again.
uint32_t body_key() {
  uint32_t key = 2166136261u;
  const auto mix = [&key](uint32_t value) { key = (key ^ value) * 16777619u; };
  mix(g_step | (g_guide ? 0x100u : 0u));
  char address[64] = "";
  if (g_step == 1 || g_step == 2) {
    const settings_model::WifiValues v = settings_model::wifi_values();
    mix(v.connected | v.access_point << 1 | v.hotspot_switching << 2 | v.connecting << 3 | v.ethernet_active << 4 |
        static_cast<uint32_t>(v.bars) << 5);
    if (g_step == 1 && !v.connected && !v.access_point) {
      // Search turns into Searching... and back.
      mix(v.scanning ? 0x200u : 0u);
      for (uint8_t i = 0; i < settings_model::wifi_network_count(); ++i) {
        const settings_model::WifiNetwork& network = settings_model::wifi_network(i);
        for (const char* c = network.ssid; *c; ++c) mix(static_cast<uint8_t>(*c));
        mix(network.bars | network.locked << 4);
      }
    }
    settings_model::panel_address(address, sizeof(address));
  }
  if (g_step == 2) {
    const settings_model::SystemValues v = settings_model::system_values();
    mix(static_cast<uint32_t>(v.pairing) << 8);
  }
  if (g_step == 3) settings_model::web_admin_url(address, sizeof(address));
  for (const char* c = address; *c; ++c) mix(static_cast<uint8_t>(*c));
  return key;
}

void build_body() {
  if (!g_content) return;
  settings_parts::close_options();
  stop_spinner();
  g_rotation = nullptr;
  for (LocaleRow& row : g_locale_rows) row = {};
  if (g_body) lv_obj_delete(g_body);
  if (g_foot) lv_obj_delete(g_foot);
  const Colors palette = colors();
  g_body = settings_parts::plain(g_content);
  lv_obj_set_pos(g_body, popup_layout::kCardPad, body_top());
  lv_obj_set_size(g_body, body_width(), body_height());
  lv_obj_set_flex_flow(g_body, LV_FLEX_FLOW_COLUMN);
  lv_obj_add_flag(g_body, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  g_key = body_key();
  switch (g_step) {
    case 0:
      build_language(palette);
      break;
    case 1:
      build_wifi(palette);
      break;
    case 2:
      build_home_assistant(palette);
      break;
    default:
      build_tiles(palette);
      break;
  }
  build_foot(palette);
}

// The card: built for the stored step, language and tile color.
void build_card() {
  const int stored = settings_model::setup_step();
  if (!g_panel || stored < 0) return;
  close_dialog();
  settings_entry::close();
  settings_parts::close_options();
  stop_spinner();
  if (g_card) lv_obj_delete(g_card);
  g_card = g_content = g_body = g_foot = nullptr;
  g_step = static_cast<uint8_t>(stored);
  g_built_card = settings_model::card_color() & 0xFFFFFF;
  g_built_text = &settings_model::text();
  const Colors palette = colors();
  g_card = settings_parts::plain(g_panel);
  lv_obj_set_pos(g_card, (SCREEN_WIDTH - card_width()) / 2 - GRID_PAD_LEFT, popup_layout::kCardMargin - GRID_PAD_TOP);
  lv_obj_set_size(g_card, card_width(), card_height());
  lv_obj_set_style_bg_color(g_card, lv_color_hex(palette.card), 0);
  lv_obj_set_style_bg_opa(g_card, LV_OPA_COVER, 0);
  settings_style::apply_tile_radius(g_card);
  ui_surface_style::apply_global_tile_border(g_card);
  g_content = settings_parts::plain(g_card);
  lv_obj_set_size(g_content, card_width(), card_height());

  char line[48];
  step_line(line, sizeof(line));
  settings_parts::step_head(g_content, card_width(), kSteps[g_step].icon, kSteps[g_step].color, g_built_card,
                            step_title(g_step), line, popup_layout::kCloseButtonSize + 24);
  settings_parts::close_button(g_content, settings_parts::popup_close_x(card_width()), settings_parts::popup_close_y(),
                               popup_layout::kCloseButtonSize, on_action, data(Action::Close));
  // Opening the WiFi step searches; the Home Assistant step makes the panel
  // findable (tick).
  if (g_step == 1) {
    settings_model::wifi_scan();
    g_scan_at = lv_tick_get();
  }
  build_body();
}

// While it shows: the WiFi entry's attempt, the state behind the body (Next
// appears once the step is done) and the pairing window.
void tick() {
  if (!g_card) return;
  const uint32_t now = lv_tick_get();
  if (settings_entry::is_open()) {
    if (settings_entry::tick(settings_model::wifi_values())) {
      show_content(true);
      build_body();
    } else {
      return;
    }
  }
  if (g_step == 1) {
    const settings_model::WifiValues v = settings_model::wifi_values();
    if (!v.connected && !v.access_point && !v.scanning && settings_model::wifi_network_count() == 0 &&
        now - g_scan_at >= kScanRetryMs) {
      g_scan_at = now;
      settings_model::wifi_scan();
    }
  }
  if (g_step == 2 && wifi_done(settings_model::wifi_values()) &&
      settings_model::system_values().pairing == PairState::NotPaired &&
      (g_pair_tried_at == 0 || now - g_pair_tried_at >= kPairRetryMs)) {
    // Findable for Home Assistant while this step shows (the window lasts
    // two minutes; it opens again when it lapsed).
    g_pair_tried_at = now | 1;
    settings_model::pair();
  }
  // A list or dialog stays as it is until it closes.
  if (body_key() != g_key && !g_dialog) build_body();
}

void on_timer(lv_timer_t*) { tick(); }

}  // namespace

void show(lv_obj_t* panel) {
  g_panel = panel;
  const int stored = settings_model::setup_step();
  if (!panel || stored < 0) return;
  if (!g_card || g_step != stored || g_built_text != &settings_model::text() ||
      g_built_card != (settings_model::card_color() & 0xFFFFFF)) {
    build_card();
  } else {
    tick();
  }
  if (!g_timer) g_timer = lv_timer_create(on_timer, 500, nullptr);
}

void hide() {
  if (g_timer) {
    lv_timer_delete(g_timer);
    g_timer = nullptr;
  }
  close_dialog();
  settings_entry::close();
  settings_parts::close_options();
  stop_spinner();
  if (g_card) lv_obj_delete(g_card);
  g_card = g_content = g_body = g_foot = nullptr;
  g_rotation = nullptr;
  for (LocaleRow& row : g_locale_rows) row = {};
  g_built_text = nullptr;
}

bool shown() { return g_card != nullptr; }

void refresh() { tick(); }

void sync_rotation() { settings_parts::segment_select(g_rotation, settings_model::rotation_index()); }

void close_overlays() {
  settings_parts::close_options();
  close_dialog();
  if (settings_entry::is_open()) {
    settings_entry::close();
    show_content(true);
  }
}

}  // namespace setup_screen
