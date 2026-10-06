#include "src/ui/tabs/settings/settings_screen.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "src/tiles/icons/mdi_icons.h"
#include "src/ui/startup/hometiles_logo.h"
#include "src/ui/tabs/settings/settings_model.h"
#include "src/ui/tabs/settings/settings_parts.h"
#include "src/ui/tabs/settings/settings_style.h"

// The approved layout (build/design-mockups/settings/settings-menu.html and
// the reference images in its sheets/ folder). The top half row is the bar in
// its Minimal style, laid out like a pill row: gear circle and title on the
// left, X on the right. Landscape panels show the four categories as tiles
// over the first two columns, sharing the height evenly (the one exception to
// the grid), with the page card beside them; square panels put them as round
// tabs into the bar beside the X; portrait panels show them as 2x1 tiles in
// the bottom two rows under the card. Only the open page is built, while
// Settings shows.
namespace settings_screen {
namespace {

using settings_style::Colors;
using settings_style::Tone;

// The categories in their fixed order (mockup CATS).
enum class Category : uint8_t { Display = 0, Wifi, Localization, System };
constexpr uint8_t kCategoryCount = 4;
constexpr uint32_t kCategoryColors[kCategoryCount] = {settings_style::kDisplayColor, settings_style::kWifiColor,
                                                      settings_style::kLocalizationColor, settings_style::kSystemColor};

enum class Layout : uint8_t { Split, Tabs, Portrait };
constexpr Layout kLayout = SCREEN_WIDTH > SCREEN_HEIGHT    ? Layout::Split
                           : SCREEN_HEIGHT > SCREEN_WIDTH ? Layout::Portrait
                                                          : Layout::Tabs;

struct CategoryView {
  lv_obj_t* box;
  lv_obj_t* disc;
  lv_obj_t* icon;
  lv_obj_t* title;
  lv_obj_t* line;
};

lv_obj_t* g_panel = nullptr;
CategoryView g_views[kCategoryCount] = {};
lv_obj_t* g_bar_title = nullptr;
lv_obj_t* g_card_title = nullptr;
// The card's body: holds the open page.
lv_obj_t* g_page = nullptr;
int g_page_width = 0;
bool g_page_built = false;
Category g_category = Category::Display;
// What the frame was built with: another tile color, Circle strength or
// language rebuilds it when Settings opens next.
uint32_t g_built_card = 0xFFFFFFFF;
uint8_t g_built_glow = 0xFF;
const i18n::Strings* g_built_text = nullptr;
// Refreshes the category lines (the time on Localization) while Settings shows.
lv_timer_t* g_lines_timer = nullptr;

// Display page.
lv_obj_t* g_brightness_value = nullptr;
lv_obj_t* g_sleep_value = nullptr;
lv_obj_t* g_saver_value = nullptr;
lv_obj_t* g_saver_brightness_value = nullptr;
lv_obj_t* g_rotation_segment = nullptr;

// Localization page: one dropdown row per list.
struct LocaleRow {
  lv_obj_t* row;
  lv_obj_t* value;
  lv_obj_t* chevron;
};
LocaleRow g_locale_rows[settings_model::kLocaleListCount] = {};
int g_card_width = 0;

// System page: what it was built for (a change rebuilds it), the labels that
// count down, the download bar, the poll timer while it shows.
uint32_t g_system_key = 0xFFFFFFFF;
lv_obj_t* g_pair_time = nullptr;
lv_obj_t* g_password_time = nullptr;
lv_obj_t* g_progress_fill = nullptr;
lv_timer_t* g_system_timer = nullptr;
// The open dialog (on the panel, over a veil).
enum class Dialog : uint8_t { None, Unpair, RemovePassword, Restart, GitHub, Pairing };
Dialog g_dialog = Dialog::None;
lv_obj_t* g_dialog_root = nullptr;
settings_model::PairState g_dialog_pair = settings_model::PairState::NotPaired;

Colors colors() { return settings_style::colors(g_built_card); }

const char* category_title(Category category) {
  const i18n::Strings& s = settings_model::text();
  switch (category) {
    case Category::Display:
      return s.display_label;
    case Category::Wifi:
      return s.wifi_label;
    case Category::Localization:
      return s.admin_settings_language;
    case Category::System:
      return "System";
  }
  return "";
}

const char* category_icon(Category category) {
  switch (category) {
    case Category::Display:
      return "monitor";
    case Category::Wifi:
      return settings_model::access_point_on() ? "access-point"
             : settings_model::network_connected() ? "wifi"
                                                   : "wifi-off";
    case Category::Localization:
      return "translate";
    case Category::System:
      return "chip";
  }
  return "cog";
}

// A sleep or screensaver step as the mockup writes it: "30 s", "5 min",
// "1 h", or Never past the last step.
void format_step(char* buf, size_t len, int index) {
  if (index >= settings_model::sleep_steps()) {
    snprintf(buf, len, "%s", settings_model::text().sleep_never);
    return;
  }
  const unsigned seconds = settings_model::sleep_step_seconds(index);
  if (seconds < 60) {
    snprintf(buf, len, "%u s", seconds);
  } else if (seconds < 3600) {
    snprintf(buf, len, "%u min", seconds / 60);
  } else {
    snprintf(buf, len, "%u h", seconds / 3600);
  }
}

// The grey line under a category's name (mockup CATS sub): brightness and
// sleep, the network, language and time, the version or a found update.
void category_line(Category category, char* buf, size_t len, bool* warn) {
  *warn = false;
  const i18n::Strings& s = settings_model::text();
  switch (category) {
    case Category::Display: {
      char step[24];
      format_step(step, sizeof(step), settings_model::saved_sleep_index());
      // "Sleep never": the step's first letter in lower case (ASCII only).
      if (step[0] >= 'A' && step[0] <= 'Z') step[0] = static_cast<char>(step[0] - 'A' + 'a');
      char sleep[48];
      snprintf(sleep, sizeof(sleep), s.settings_sleep_summary_fmt, step);
      snprintf(buf, len, "%d %% \xC2\xB7 %s", settings_model::saved_brightness(), sleep);
      return;
    }
    case Category::Wifi:
      if (settings_model::access_point_on()) {
        snprintf(buf, len, "%s", s.settings_access_point_on);
      } else if (settings_model::network_connected()) {
        settings_model::network_name(buf, len);
      } else {
        snprintf(buf, len, "%s", s.settings_not_connected);
      }
      return;
    case Category::Localization: {
      char time[16];
      if (settings_model::time_text(time, sizeof(time))) {
        snprintf(buf, len, "%s \xC2\xB7 %s", settings_model::language_name(), time);
      } else {
        snprintf(buf, len, "%s", settings_model::language_name());
      }
      return;
    }
    case Category::System:
      if (settings_model::update_available()) {
        *warn = true;
        snprintf(buf, len, "%s", s.settings_update_available);
      } else {
        snprintf(buf, len, "%s", settings_model::firmware_version());
      }
      return;
  }
  buf[0] = '\0';
}

void on_lines_timer(lv_timer_t*) { refresh_lines(); }

// Selected = the tile in its color (tile color "From icon") or the tab in
// its circle; the others on the plain card.
void style_category(uint8_t index) {
  CategoryView& view = g_views[index];
  if (!view.box) return;
  const bool selected = index == static_cast<uint8_t>(g_category);
  const uint32_t color = kCategoryColors[index];
  const uint32_t card = g_built_card;
  if (kLayout == Layout::Tabs) {
    const Tone tone = settings_style::tone(card, color);
    lv_obj_set_style_bg_color(view.box, lv_color_hex(tone.disc), 0);
    lv_obj_set_style_bg_opa(view.box, selected ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(view.box, lv_color_hex(selected ? tone.disc : colors().button), LV_STATE_PRESSED);
    if (view.icon) lv_obj_set_style_text_color(view.icon, lv_color_hex(tone.icon), 0);
    return;
  }
  const uint32_t tile = selected ? settings_style::from_icon_card(card, color) : card;
  const uint32_t pressed = selected ? settings_style::from_icon_card(card, color, true)
                                    : tone_color::lifted(card, card, false, settings_style::control_step());
  const Tone tone = settings_style::tone(tile, color);
  lv_obj_set_style_bg_color(view.box, lv_color_hex(tile), 0);
  lv_obj_set_style_bg_color(view.box, lv_color_hex(pressed), LV_STATE_PRESSED);
  if (view.disc) lv_obj_set_style_bg_color(view.disc, lv_color_hex(tone.disc), 0);
  if (view.icon) lv_obj_set_style_text_color(view.icon, lv_color_hex(tone.icon), 0);
}

// A circle with an MDI icon in its middle.
lv_obj_t* circle(lv_obj_t* parent, int x, int y, int diameter, const char* icon, uint32_t icon_color) {
  lv_obj_t* obj = settings_parts::plain(parent);
  lv_obj_set_pos(obj, x, y);
  lv_obj_set_size(obj, diameter, diameter);
  lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
  lv_obj_t* label = lv_label_create(obj);
  lv_label_set_text(label, getMdiChar(icon).c_str());
  if (FONT_MDI_ICONS) lv_obj_set_style_text_font(label, FONT_MDI_ICONS, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(icon_color), 0);
  lv_obj_center(label);
  return obj;
}

// A round bar button: no fill at rest, the button step while pressed.
void make_round_button(lv_obj_t* obj, lv_event_cb_t on_click, void* user_data) {
  lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(obj, lv_color_hex(colors().button), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_ext_click_area(obj, settings_style::tile_inset());
  lv_obj_add_event_cb(obj, on_click, LV_EVENT_CLICKED, user_data);
}

void on_close_clicked(lv_event_t*) { settings_model::close_settings(); }

void select_category(Category category);

// A category tile or tab opens its page in the card; WiFi still opens its
// popup until its page follows.
void on_category_clicked(lv_event_t* e) {
  const uintptr_t raw = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
  if (raw >= kCategoryCount) return;
  const Category category = static_cast<Category>(raw);
  if (category == Category::Wifi) {
    settings_model::open_category_popup(static_cast<uint8_t>(raw), e);
    return;
  }
  select_category(category);
}

// A category tile: circle, name and the grey line (mockup catTileBig).
void build_category_tile(lv_obj_t* panel, uint8_t index, int x, int y, int w, int h) {
  CategoryView& view = g_views[index];
  const Category category = static_cast<Category>(index);
  view.box = settings_parts::plain(panel);
  lv_obj_add_flag(view.box, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_pos(view.box, x, y);
  lv_obj_set_size(view.box, w, h);
  lv_obj_set_style_bg_opa(view.box, LV_OPA_COVER, 0);
  settings_style::apply_tile_radius(view.box);
  ui_surface_style::apply_global_tile_border(view.box);
  lv_obj_add_event_cb(view.box, on_category_clicked, LV_EVENT_CLICKED,
                      reinterpret_cast<void*>(static_cast<uintptr_t>(index)));
  const int disc = settings_style::kCategoryDisc;
  const int x0 = settings_style::kCategoryPad;
  view.disc = circle(view.box, x0, (h - disc) / 2, disc, category_icon(category), 0xFFFFFF);
  lv_obj_set_style_bg_opa(view.disc, LV_OPA_COVER, 0);
  view.icon = lv_obj_get_child(view.disc, 0);
  // Name and line in line boxes of 1.21 and 1.3 times their font size,
  // centered together on the tile.
  const int text_x = x0 + disc + settings_style::kCategoryTextGap;
  const int text_w = w - text_x - x0;
  const int box1 = (settings_style::kRowFontPx * 121 + 50) / 100;
  const int box2 = (settings_style::kSmallFontPx * 13 + 5) / 10;
  const int top = (h - box1 - box2 + 1) / 2;
  view.title = lv_label_create(view.box);
  lv_label_set_text(view.title, category_title(category));
  lv_label_set_long_mode(view.title, LV_LABEL_LONG_DOT);
  lv_obj_set_width(view.title, text_w);
  lv_obj_set_style_text_font(view.title, settings_style::row_font(), 0);
  lv_obj_set_style_text_color(view.title, lv_color_white(), 0);
  lv_obj_set_pos(view.title, text_x,
                 settings_parts::browser_label_y(settings_style::row_font(), settings_style::kRowFontPx, top, box1));
  view.line = lv_label_create(view.box);
  lv_label_set_text(view.line, "");
  lv_label_set_long_mode(view.line, LV_LABEL_LONG_DOT);
  lv_obj_set_width(view.line, text_w);
  lv_obj_set_style_text_font(view.line, settings_style::small_font(), 0);
  lv_obj_set_pos(view.line, text_x,
                 settings_parts::browser_label_y(settings_style::small_font(), settings_style::kSmallFontPx,
                                                 top + box1, box2));
}

// ---------- Display page ----------
// Screen: Brightness, Sleep, Rotation; Screensaver: Starts after, Brightness
// (mockup pageDisplay). All sliders start at the same x; the rotation offers
// the quarter turns where the panel supports them.

void clear_display_refs() {
  g_brightness_value = nullptr;
  g_sleep_value = nullptr;
  g_saver_value = nullptr;
  g_saver_brightness_value = nullptr;
  g_rotation_segment = nullptr;
}

void set_percent_text(lv_obj_t* label, int percent) {
  if (!label) return;
  char buf[16];
  snprintf(buf, sizeof(buf), "%d %%", percent);
  lv_label_set_text(label, buf);
}

void set_step_text(lv_obj_t* label, int index) {
  if (!label) return;
  char buf[24];
  format_step(buf, sizeof(buf), index);
  lv_label_set_text(label, buf);
}

void on_brightness(lv_obj_t*, int32_t value, bool final) {
  set_percent_text(g_brightness_value, value);
  settings_model::brightness_changed(value, final);
  if (final) refresh_lines();
}

void on_sleep(lv_obj_t*, int32_t value, bool final) {
  set_step_text(g_sleep_value, value);
  settings_model::sleep_changed(value, final);
  if (final) refresh_lines();
}

void on_saver(lv_obj_t*, int32_t value, bool final) {
  set_step_text(g_saver_value, value);
  settings_model::saver_changed(value, final);
}

void on_saver_brightness(lv_obj_t*, int32_t value, bool final) {
  set_percent_text(g_saver_brightness_value, value);
  settings_model::saver_brightness_changed(value, final);
}

void on_rotation(lv_obj_t*, uint8_t index) { settings_model::rotation_selected(index); }

// One slider length per panel: what a row leaves beside the longest slider
// name (+30 % headroom), so every slider starts at the same x (mockup
// applyProfile).
int display_slider_width(int page_width) {
  const i18n::Strings& s = settings_model::text();
  int name = 0;
  for (const char* text : {s.settings_brightness, s.settings_sleep, s.settings_starts_after}) {
    const int width = settings_parts::text_width(settings_style::row_font(), text);
    if (width > name) name = width;
  }
  name = name * 13 / 10;
  const int room = page_width - settings_style::kRowPadLeft - settings_style::kRowPadRight -
                   settings_parts::icon_width() - 3 * settings_style::kRowGap -
                   settings_style::kValueWidth - name;
  int width = room < settings_style::kSliderMaxWidth ? room : settings_style::kSliderMaxWidth;
  if (width < settings_style::kSliderHeight * 3) width = settings_style::kSliderHeight * 3;
  return width;
}

// A slider row: grey icon, name, slider in the page color on its control
// track (the Light popup's brightness track), value.
lv_obj_t* display_slider_row(lv_obj_t* group, const char* icon, const char* title, int width, int32_t min,
                             int32_t max, int32_t value, settings_parts::SliderCallback on_change,
                             lv_obj_t** value_label) {
  const Colors palette = colors();
  const Tone tone = settings_style::tone(palette.card, settings_style::kDisplayColor);
  settings_parts::Row row = settings_parts::row(group, icon, title);
  lv_obj_t* slider = settings_parts::slider(row.row, width, min, max, value, settings_style::kDisplayColor,
                                            tone.control, palette.card, on_change);
  *value_label = settings_parts::value_label(row.row, "");
  return slider;
}

void build_display_page(lv_obj_t* page) {
  const i18n::Strings& s = settings_model::text();
  const settings_model::DisplayValues values = settings_model::display_values();
  const Colors palette = colors();
  const int width = display_slider_width(g_page_width);
  const int never = settings_model::sleep_steps();

  settings_parts::section(page, s.settings_screen, true);
  lv_obj_t* screen = settings_parts::group(page, palette);
  display_slider_row(screen, "brightness-6", s.settings_brightness, width, values.brightness_min,
                     values.brightness_max, values.brightness, on_brightness, &g_brightness_value);
  set_percent_text(g_brightness_value, values.brightness);
  display_slider_row(screen, "power-sleep", s.settings_sleep, width, 0, never, values.sleep_index, on_sleep,
                     &g_sleep_value);
  set_step_text(g_sleep_value, values.sleep_index);
  settings_parts::Row rotation = settings_parts::row(screen, "screen-rotation", s.settings_rotation);
  static const char* const kQuarterTurns[] = {"0\xC2\xB0", "90\xC2\xB0", "180\xC2\xB0", "270\xC2\xB0"};
  static const char* const kFlip[] = {"0\xC2\xB0", "180\xC2\xB0"};
  if (settings_model::quarter_turns()) {
    g_rotation_segment = settings_parts::segment(rotation.row, kQuarterTurns, 4, settings_model::rotation_index(),
                                                 palette.card, on_rotation, settings_style::kQuarterSegmentMinWidth,
                                                 settings_style::kQuarterSegmentPad);
  } else {
    g_rotation_segment =
        settings_parts::segment(rotation.row, kFlip, 2, settings_model::rotation_index(), palette.card, on_rotation);
  }

  settings_parts::section(page, s.settings_screensaver, false);
  lv_obj_t* saver = settings_parts::group(page, palette);
  display_slider_row(saver, "timer-outline", s.settings_starts_after, width, 0, never, values.saver_index, on_saver,
                     &g_saver_value);
  set_step_text(g_saver_value, values.saver_index);
  display_slider_row(saver, "brightness-4", s.settings_brightness, width, values.saver_brightness_min,
                     values.saver_brightness_max, values.saver_brightness, on_saver_brightness,
                     &g_saver_brightness_value);
  set_percent_text(g_saver_brightness_value, values.saver_brightness);
}

// ---------- Localization page ----------
// Language, Time zone, Time format, Date format, Keyboard as dropdown rows
// (mockup pageLocalization, no Preview group): the value on the right, or
// under the label on narrow cards; a tap opens the option list.

using settings_model::LocaleList;

void clear_locale_refs() {
  settings_parts::close_options();
  for (LocaleRow& row : g_locale_rows) row = {};
}

const char* locale_title(LocaleList list) {
  const i18n::Strings& s = settings_model::text();
  switch (list) {
    case LocaleList::Language:
      return s.settings_language;
    case LocaleList::TimeZone:
      return s.settings_time_zone;
    case LocaleList::TimeFormat:
      return s.settings_time_format;
    case LocaleList::DateFormat:
      return s.settings_date_format;
    case LocaleList::Keyboard:
      return s.settings_keyboard;
  }
  return "";
}

const char* locale_icon(LocaleList list) {
  switch (list) {
    case LocaleList::Language:
      return "translate";
    case LocaleList::TimeZone:
      return "earth";
    case LocaleList::TimeFormat:
      return "clock-outline";
    case LocaleList::DateFormat:
      return "calendar-blank-outline";
    case LocaleList::Keyboard:
      return "keyboard-outline";
  }
  return "translate";
}

const char* locale_value(LocaleList list) {
  return settings_model::locale_option(list, settings_model::locale_selected(list));
}

const char* option_text(uint8_t tag, uint8_t index) {
  return settings_model::locale_option(static_cast<LocaleList>(tag), index);
}

void open_locale_list(uint8_t index);

void set_chevron(uint8_t index, bool up) {
  if (index >= settings_model::kLocaleListCount || !g_locale_rows[index].chevron) return;
  lv_label_set_text(g_locale_rows[index].chevron, getMdiChar(up ? "chevron-up" : "chevron-down").c_str());
}

// A tap beside the list closed it; one on another dropdown row opens that
// row's list right away (mockup: the same tap switches lists).
void on_options_closed(uint8_t tag, const lv_point_t* tapped) {
  set_chevron(tag, false);
  if (!tapped) return;
  for (uint8_t i = 0; i < settings_model::kLocaleListCount; ++i) {
    lv_obj_t* row = g_locale_rows[i].row;
    if (i == tag || !row) continue;
    lv_area_t area;
    lv_obj_get_coords(row, &area);
    if (tapped->x >= area.x1 && tapped->x <= area.x2 && tapped->y >= area.y1 && tapped->y <= area.y2) {
      open_locale_list(i);
      return;
    }
  }
}

void on_option_picked(uint8_t tag, uint8_t index) {
  const LocaleList list = static_cast<LocaleList>(tag);
  if (index == settings_model::locale_selected(list)) return;
  // A new language rebuilds the whole screen in it (texts_changed).
  settings_model::locale_selected_changed(list, index);
  if (tag < settings_model::kLocaleListCount && g_locale_rows[tag].value) {
    lv_label_set_text(g_locale_rows[tag].value, locale_value(list));
  }
  refresh_lines();
}

void open_locale_list(uint8_t index) {
  if (index >= settings_model::kLocaleListCount || !g_locale_rows[index].row || !g_page) return;
  const LocaleList list = static_cast<LocaleList>(index);
  settings_parts::OptionList spec = {};
  spec.host = g_panel;
  spec.row = g_locale_rows[index].row;
  lv_obj_update_layout(g_panel);
  lv_obj_get_coords(g_page, &spec.bounds);
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

void on_locale_row_clicked(lv_event_t* e) {
  open_locale_list(static_cast<uint8_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e))));
}

void build_localization_page(lv_obj_t* page) {
  const Colors palette = colors();
  // Narrow cards put the value under the label, so long translations never
  // meet it (mockup P.stack).
  const bool stacked = g_card_width < settings_style::kStackedCardWidth;
  const int value_width = (g_page_width - settings_style::kRowPadLeft - settings_style::kRowPadRight) / 2;
  lv_obj_t* group = settings_parts::group(page, palette);
  for (uint8_t i = 0; i < settings_model::kLocaleListCount; ++i) {
    const LocaleList list = static_cast<LocaleList>(i);
    LocaleRow& view = g_locale_rows[i];
    settings_parts::Row row =
        settings_parts::row(group, locale_icon(list), locale_title(list), stacked ? locale_value(list) : nullptr);
    view.row = row.row;
    view.value = stacked ? row.sub : settings_parts::trailing_text(row.row, locale_value(list), value_width);
    view.chevron = settings_parts::trailing_icon(row.row, "chevron-down");
    settings_parts::make_tap(row.row, palette.button, on_locale_row_clicked,
                             reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
  }
}

// ---------- System page ----------
// The HomeTiles head (logo, name and version, the device or the update's
// state, the update button), Connection (Home Assistant and the panel's
// address), Security (Pairing, Web Admin password), then Setup, Restart and
// GitHub at the card's bottom (mockup pageSystem). Questions, the pairing
// number and the GitHub QR code open dialogs (mockup dialog()).

using settings_model::PairState;
using settings_model::SystemValues;
using settings_model::UpdateState;

lv_obj_t* g_address_label = nullptr;

enum class SystemAction : uint8_t { Update, Pair, Unpair, Allow, RemovePassword, Setup, Restart, GitHub };
enum class DialogAction : uint8_t { Close, Unpair, RemovePassword, Restart, CancelPairing, ConfirmPairing };

void build_page();
void system_tick();

void clear_system_refs() {
  g_pair_time = nullptr;
  g_password_time = nullptr;
  g_progress_fill = nullptr;
  g_address_label = nullptr;
  g_system_key = 0xFFFFFFFF;
}

void stop_system_timer() {
  if (!g_system_timer) return;
  lv_timer_delete(g_system_timer);
  g_system_timer = nullptr;
}

// Inside a touch on the dialog it goes after the event.
void close_dialog(bool from_event = false) {
  lv_obj_t* root = g_dialog_root;
  g_dialog_root = nullptr;
  g_dialog = Dialog::None;
  if (!root) return;
  if (from_event) {
    lv_obj_delete_async(root);
  } else {
    lv_obj_delete(root);
  }
}

bool address_known() {
  char address[48];
  return settings_model::panel_address(address, sizeof(address));
}

// What the page shows apart from the countdowns, the download bar and the
// address text; a change rebuilds it.
uint32_t system_key(const SystemValues& v) {
  return static_cast<uint32_t>(v.update) | static_cast<uint32_t>(v.pairing) << 4 | (v.connected ? 1u : 0u) << 8 |
         (v.password_on ? 1u : 0u) << 9 | (v.password_window ? 1u : 0u) << 10 |
         (settings_model::pairing_note() ? 1u : 0u) << 11 | (address_known() ? 1u : 0u) << 12;
}

void set_countdown(lv_obj_t* label, int seconds) {
  if (!label) return;
  char buf[12] = "";
  if (seconds > 0) snprintf(buf, sizeof(buf), "%d:%02d", seconds / 60, seconds % 60);
  if (strcmp(lv_label_get_text(label), buf) != 0) lv_label_set_text(label, buf);
}

void set_progress(int percent) {
  if (!g_progress_fill) return;
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  lv_obj_set_width(g_progress_fill, settings_style::kProgressWidth * percent / 100);
}

void color_text(lv_obj_t* label, uint32_t color) {
  if (!label) return;
  lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
  lv_obj_set_style_text_opa(label, LV_OPA_COVER, 0);
}

// A state on the right of a row in green with a check ("Connected", "Paired",
// "On"), and a chevron when a tap on the row asks to turn it off.
void good_state(lv_obj_t* row, const char* text, bool chevron) {
  color_text(settings_parts::trailing_text(row, text, g_page_width / 2), settings_style::kGoodColor);
  color_text(settings_parts::trailing_icon(row, "check"), settings_style::kGoodColor);
  if (chevron) settings_parts::trailing_icon(row, "chevron-right");
}

// The grey countdown of a two-minute window on the right of a row.
lv_obj_t* countdown(lv_obj_t* row, int seconds) {
  lv_obj_t* label = settings_parts::trailing_text(row, "", 0);
  lv_obj_set_width(label, LV_SIZE_CONTENT);
  set_countdown(label, seconds);
  return label;
}

void* action_data(SystemAction action) { return reinterpret_cast<void*>(static_cast<uintptr_t>(action)); }

void on_system_action(lv_event_t* e);

lv_obj_t* row_button(lv_obj_t* row, const char* text, const char* icon, settings_parts::ButtonKind kind,
                     SystemAction action) {
  return settings_parts::button(row, text, icon, kind, settings_style::kSystemColor, colors(),
                                settings_style::kButtonHeight, false, on_system_action, action_data(action));
}

void refresh_async(void*) { system_tick(); }

void open_dialog(Dialog dialog);

void on_system_action(lv_event_t* e) {
  switch (static_cast<SystemAction>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)))) {
    case SystemAction::Update:
      settings_model::update_pressed();
      break;
    case SystemAction::Pair:
      settings_model::pair();
      break;
    case SystemAction::Allow:
      settings_model::allow_password();
      break;
    case SystemAction::Unpair:
      open_dialog(Dialog::Unpair);
      return;
    case SystemAction::RemovePassword:
      open_dialog(Dialog::RemovePassword);
      return;
    case SystemAction::Restart:
      open_dialog(Dialog::Restart);
      return;
    case SystemAction::GitHub:
      open_dialog(Dialog::GitHub);
      return;
    case SystemAction::Setup:
      return;
  }
  // The page may be rebuilt, the touched button with it: after the event.
  lv_async_call(refresh_async, nullptr);
}

void on_dialog_action(lv_event_t* e) {
  const auto action = static_cast<DialogAction>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  // Confirm keeps the dialog: it turns into the wait for Home Assistant.
  if (action != DialogAction::ConfirmPairing) close_dialog(true);
  switch (action) {
    case DialogAction::Close:
      break;
    case DialogAction::Unpair:
      settings_model::unpair();
      break;
    case DialogAction::RemovePassword:
      settings_model::remove_password();
      break;
    case DialogAction::Restart:
      settings_model::restart();
      break;
    case DialogAction::CancelPairing:
      settings_model::cancel_pairing();
      break;
    case DialogAction::ConfirmPairing:
      settings_model::confirm_pairing();
      break;
  }
  lv_async_call(refresh_async, nullptr);
}

// A tap beside a question closes it; the pairing number stays until Cancel.
void on_veil(lv_event_t*) {
  if (g_dialog == Dialog::Pairing) return;
  close_dialog(true);
}

lv_obj_t* dialog_button(lv_obj_t* row, const char* text, const char* icon, settings_parts::ButtonKind kind,
                        DialogAction action) {
  lv_obj_t* b = settings_parts::button(row, text, icon, kind, settings_style::kSystemColor, colors(),
                                       settings_style::kDialogButtonHeight, true, on_dialog_action,
                                       reinterpret_cast<void*>(static_cast<uintptr_t>(action)));
  lv_obj_set_flex_grow(b, 1);
  return b;
}

void open_dialog(Dialog dialog) {
  close_dialog();
  settings_parts::close_options();
  const i18n::Strings& s = settings_model::text();
  using settings_parts::ButtonKind;
  lv_obj_t* box = nullptr;
  lv_obj_t* buttons = nullptr;
  switch (dialog) {
    case Dialog::None:
      return;
    case Dialog::Unpair:
      box = settings_parts::dialog(g_panel, g_built_card, s.settings_unpair_question, on_veil);
      settings_parts::dialog_text(box, s.settings_unpair_text);
      buttons = settings_parts::dialog_buttons(box);
      dialog_button(buttons, s.security_cancel, nullptr, ButtonKind::Normal, DialogAction::Close);
      dialog_button(buttons, s.settings_unpair, nullptr, ButtonKind::Danger, DialogAction::Unpair);
      break;
    case Dialog::RemovePassword:
      box = settings_parts::dialog(g_panel, g_built_card, s.security_password_question, on_veil);
      settings_parts::dialog_text(box, s.security_password_question_hint);
      buttons = settings_parts::dialog_buttons(box);
      dialog_button(buttons, s.security_cancel, nullptr, ButtonKind::Normal, DialogAction::Close);
      dialog_button(buttons, s.security_remove, nullptr, ButtonKind::Danger, DialogAction::RemovePassword);
      break;
    case Dialog::Restart:
      box = settings_parts::dialog(g_panel, g_built_card, s.settings_restart_question, on_veil);
      settings_parts::dialog_text(box, s.settings_restart_text);
      buttons = settings_parts::dialog_buttons(box);
      dialog_button(buttons, s.security_cancel, nullptr, ButtonKind::Normal, DialogAction::Close);
      dialog_button(buttons, s.restart_button, nullptr, ButtonKind::Danger, DialogAction::Restart);
      break;
    case Dialog::GitHub: {
      box = settings_parts::dialog(g_panel, g_built_card, settings_style::kGitHub, on_veil);
      const char* url = settings_model::repo_url();
      lv_obj_t* qr = settings_parts::qr_code(box, settings_style::kDialogQr, url);
      if (qr) lv_obj_set_style_margin_top(qr, settings_style::kDialogGap, 0);
      // The address without its scheme, as people type it.
      const char* shown = strstr(url, "://") ? strstr(url, "://") + 3 : url;
      lv_obj_t* link = settings_parts::dialog_text(box, shown);
      lv_obj_set_style_text_color(link, lv_color_white(), 0);
      lv_obj_set_style_text_opa(link, LV_OPA_COVER, 0);
      settings_parts::dialog_text(box, s.settings_github_text);
      buttons = settings_parts::dialog_buttons(box);
      dialog_button(buttons, s.security_close, nullptr, ButtonKind::Normal, DialogAction::Close);
      break;
    }
    case Dialog::Pairing: {
      box = settings_parts::dialog(g_panel, g_built_card, s.settings_pairing, on_veil);
      char number[24];
      if (g_dialog_pair != PairState::Asking && settings_model::pairing_number(number, sizeof(number))) {
        lv_obj_t* code = lv_label_create(box);
        lv_label_set_text(code, number);
        lv_obj_set_style_text_font(code, popup_layout::font72(), 0);
        lv_obj_set_style_text_color(code, lv_color_white(), 0);
        lv_obj_set_style_margin_top(code, settings_style::kDialogGap, 0);
      }
      char text[200];
      if (g_dialog_pair == PairState::Compare) {
        snprintf(text, sizeof(text), "%s. %s", s.pairing_compare, s.pairing_compare_hint);
      } else {
        snprintf(text, sizeof(text), "%s", g_dialog_pair == PairState::Asking ? s.pairing_asking : s.pairing_waiting);
      }
      settings_parts::dialog_text(box, text);
      buttons = settings_parts::dialog_buttons(box);
      dialog_button(buttons, s.security_cancel, nullptr, ButtonKind::Normal, DialogAction::CancelPairing);
      if (g_dialog_pair != PairState::Asking) {
        lv_obj_t* confirm =
            dialog_button(buttons, s.security_confirm, "check", ButtonKind::Accent, DialogAction::ConfirmPairing);
        settings_parts::button_set_enabled(confirm, g_dialog_pair == PairState::Compare);
      }
      break;
    }
  }
  g_dialog = dialog;
  g_dialog_root = box ? lv_obj_get_parent(box) : nullptr;
}

// The number dialog follows the pairing attempt; a question whose state went
// away closes.
void sync_dialog(const SystemValues& v) {
  const bool numbered =
      v.pairing == PairState::Asking || v.pairing == PairState::Compare || v.pairing == PairState::Confirmed;
  if (numbered) {
    if (g_dialog != Dialog::Pairing || g_dialog_pair != v.pairing) {
      g_dialog_pair = v.pairing;
      open_dialog(Dialog::Pairing);
    }
    return;
  }
  if (g_dialog == Dialog::Pairing || (g_dialog == Dialog::Unpair && v.pairing != PairState::Paired) ||
      (g_dialog == Dialog::RemovePassword && !v.password_on)) {
    close_dialog();
  }
}

void build_system_head(lv_obj_t* page, const SystemValues& v, const Colors& palette) {
  const i18n::Strings& s = settings_model::text();
  using settings_parts::ButtonKind;
  // Wide cards: the name large with the version in the row font; narrow ones
  // keep the row font with the version small (mockup pageSystem).
  const bool wide = g_card_width >= settings_style::kWideHeadCard;
  lv_obj_t* group = settings_parts::group(page, palette);
  settings_parts::Row head = settings_parts::row(group, nullptr, "", "");
  const int pad = wide ? popup_layout::scale(GRID_ROWS < 5 ? 18 : 20) : 13;
  lv_obj_set_height(head.row, settings_style::kHeroLogo + 2 * pad);

  lv_obj_t* logo = lv_image_create(head.row);
  lv_obj_remove_style_all(logo);
  lv_image_set_src(logo, &hometiles_logo_dsc);
  lv_image_set_antialias(logo, true);
  lv_image_set_scale(logo, static_cast<uint32_t>(settings_style::kHeroLogo * 256 / hometiles_logo_dsc.header.w));
  lv_obj_set_size(logo, settings_style::kHeroLogo, settings_style::kHeroLogo);
  lv_obj_move_to_index(logo, 0);

  // The product name and the version on one baseline.
  lv_obj_delete(head.title);
  head.title = nullptr;
  lv_obj_t* line = settings_parts::plain(head.text);
  lv_obj_move_to_index(line, 0);
  lv_obj_set_size(line, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(line, LV_FLEX_FLOW_ROW);
  lv_obj_add_flag(line, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  const lv_font_t* name_font = wide ? settings_style::page_title_font() : settings_style::row_font();
  const lv_font_t* version_font = wide ? settings_style::row_font() : settings_style::small_font();
  lv_obj_t* name = lv_label_create(line);
  lv_label_set_text(name, settings_style::kProductName);
  lv_obj_set_style_text_font(name, name_font, 0);
  lv_obj_set_style_text_color(name, lv_color_white(), 0);
  settings_parts::browser_line(name, wide ? settings_style::kTitleFontPx : settings_style::kRowFontPx);
  lv_obj_t* version = lv_label_create(line);
  lv_label_set_text(version, settings_model::firmware_version());
  lv_obj_set_style_text_font(version, version_font, 0);
  lv_obj_set_style_text_color(version, lv_color_white(), 0);
  lv_obj_set_style_text_opa(version, settings_style::kGreyOpa, 0);
  // One space of the name's font (a lone space measures 0).
  lv_obj_set_style_margin_left(
      version, settings_parts::text_width(name_font, "H H") - settings_parts::text_width(name_font, "HH"), 0);
  const int name_baseline = lv_obj_get_style_margin_top(name, LV_PART_MAIN) + name_font->line_height - name_font->base_line;
  lv_obj_set_style_margin_top(version, name_baseline - (version_font->line_height - version_font->base_line), 0);

  char buf[96];
  const char* sub = settings_model::device_name();
  uint32_t sub_color = 0;
  const char* label = s.system_check_updates_btn;
  const char* icon = "magnify";
  ButtonKind kind = ButtonKind::Normal;
  bool has_button = true;
  bool enabled = true;
  const bool found = settings_model::latest_version()[0] != '\0';
  char install[64];
  snprintf(install, sizeof(install), s.system_install_btn_fmt, settings_model::latest_version());
  switch (v.update) {
    case UpdateState::Idle:
      break;
    case UpdateState::Checking:
      sub = s.system_checking;
      enabled = false;
      break;
    case UpdateState::Available:
      snprintf(buf, sizeof(buf), s.system_update_available_fmt, settings_model::latest_version());
      sub = buf;
      sub_color = settings_style::kWarnColor;
      label = install;
      icon = "download";
      kind = ButtonKind::Accent;
      break;
    case UpdateState::UpToDate:
      sub = s.system_up_to_date;
      sub_color = settings_style::kGoodColor;
      break;
    case UpdateState::CheckFailed:
      sub = s.system_check_failed;
      sub_color = settings_style::kErrorColor;
      break;
    case UpdateState::Downloading:
      sub = s.system_downloading;
      has_button = false;
      break;
    case UpdateState::Installed:
      sub = s.system_installed_restarting;
      sub_color = settings_style::kGoodColor;
      has_button = false;
      break;
    case UpdateState::InstallFailed:
      sub = s.system_install_failed;
      sub_color = settings_style::kErrorColor;
      if (found) {
        label = install;
        icon = "download";
        kind = ButtonKind::Accent;
      }
      break;
    case UpdateState::Restarting:
      sub = s.system_restarting;
      has_button = false;
      break;
  }
  lv_label_set_text(head.sub, sub);
  if (sub_color) color_text(head.sub, sub_color);
  if (has_button) {
    settings_parts::button_set_enabled(row_button(head.row, label, icon, kind, SystemAction::Update), enabled);
  } else if (v.update == UpdateState::Downloading) {
    lv_obj_t* track = settings_parts::plain(head.row);
    lv_obj_set_size(track, settings_style::kProgressWidth, settings_style::kProgressHeight);
    lv_obj_set_style_bg_color(track, lv_color_hex(palette.card), 0);
    lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(track, settings_style::kProgressHeight / 2, 0);
    g_progress_fill = settings_parts::plain(track);
    lv_obj_set_height(g_progress_fill, settings_style::kProgressHeight);
    lv_obj_set_style_bg_color(g_progress_fill, lv_color_hex(settings_style::kSystemColor), 0);
    lv_obj_set_style_bg_opa(g_progress_fill, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(g_progress_fill, settings_style::kProgressHeight / 2, 0);
    set_progress(v.progress);
  }
}

void build_system_page(lv_obj_t* page) {
  const i18n::Strings& s = settings_model::text();
  const SystemValues v = settings_model::system_values();
  const Colors palette = colors();
  using settings_parts::ButtonKind;
  g_system_key = system_key(v);
  build_system_head(page, v, palette);
  lv_obj_t* head_row = lv_obj_get_child(lv_obj_get_child(page, 0), 0);
  const int head_height = lv_obj_get_style_height(head_row, LV_PART_MAIN);

  // Connection: Home Assistant, with the panel's own address under it.
  settings_parts::section(page, s.settings_connection, false);
  lv_obj_t* connection = settings_parts::group(page, palette);
  char address[48];
  char sub[64] = "";
  if (settings_model::panel_address(address, sizeof(address))) snprintf(sub, sizeof(sub), s.settings_ip_fmt, address);
  settings_parts::Row ha =
      settings_parts::row(connection, "lan-connect", settings_style::kHomeAssistant, sub[0] ? sub : nullptr);
  g_address_label = ha.sub;
  if (v.connected) {
    good_state(ha.row, s.security_value_connected, false);
  } else {
    settings_parts::trailing_text(ha.row, s.security_value_offline, g_page_width / 2);
  }

  // Security: Pairing and the Web Admin password.
  settings_parts::section(page, s.security_btn, false);
  lv_obj_t* security = settings_parts::group(page, palette);
  switch (v.pairing) {
    case PairState::Paired: {
      settings_parts::Row r =
          settings_parts::row(security, "link-variant", s.settings_pairing, s.settings_commands_encrypted);
      good_state(r.row, s.settings_paired, true);
      settings_parts::make_tap(r.row, palette.button, on_system_action,
                               action_data(SystemAction::Unpair));
      break;
    }
    case PairState::Discoverable: {
      settings_parts::Row r = settings_parts::row(security, "link-variant", s.settings_pairing, s.pairing_discoverable);
      color_text(r.sub, settings_style::kGoodColor);
      g_pair_time = countdown(r.row, v.pair_seconds);
      break;
    }
    case PairState::Asking:
    case PairState::Compare:
    case PairState::Confirmed:
      settings_parts::row(security, "link-variant", s.settings_pairing,
                          v.pairing == PairState::Asking ? s.pairing_asking : s.pairing_waiting);
      break;
    default: {
      // Not paired, or a failed attempt's result; Pair tries again.
      const char* note = settings_model::pairing_note();
      const char* line = s.settings_not_paired;
      uint32_t color = note ? settings_style::kWarnColor : 0;
      switch (v.pairing) {
        case PairState::NoAnswer:
          line = s.pairing_no_answer;
          color = settings_style::kWarnColor;
          break;
        case PairState::AlreadyPaired:
          line = s.pairing_already_paired;
          color = settings_style::kWarnColor;
          break;
        case PairState::Busy:
          line = s.pairing_busy;
          color = settings_style::kWarnColor;
          break;
        case PairState::Rejected:
          line = s.pairing_rejected;
          color = settings_style::kErrorColor;
          break;
        case PairState::Failed:
          line = s.pairing_failed;
          color = settings_style::kErrorColor;
          break;
        default:
          if (note) line = note;
          break;
      }
      settings_parts::Row r = settings_parts::row(security, "link-variant-off", s.settings_pairing, line);
      if (color) color_text(r.sub, color);
      row_button(r.row, s.settings_pair, nullptr, ButtonKind::Accent, SystemAction::Pair);
      break;
    }
  }
  if (v.password_on) {
    settings_parts::Row r =
        settings_parts::row(security, "form-textbox-password", s.web_auth_section, s.settings_password_asks);
    good_state(r.row, s.security_state_on, true);
    settings_parts::make_tap(r.row, palette.button, on_system_action,
                             action_data(SystemAction::RemovePassword));
  } else if (v.password_window) {
    settings_parts::Row r =
        settings_parts::row(security, "form-textbox-password", s.web_auth_section, s.settings_set_password_now);
    color_text(r.sub, settings_style::kGoodColor);
    g_password_time = countdown(r.row, v.password_seconds);
  } else {
    settings_parts::Row r =
        settings_parts::row(security, "form-textbox-password", s.web_auth_section, s.settings_password_none);
    row_button(r.row, s.settings_allow, nullptr, ButtonKind::Accent, SystemAction::Allow);
  }

  // Setup, Restart and GitHub right on the card (no group, no heading).
  lv_obj_t* actions = settings_parts::plain(page);
  lv_obj_set_size(actions, LV_PCT(100), settings_style::kSystemButtonHeight);
  lv_obj_set_style_margin_top(actions, settings_style::kSectionTop, 0);
  lv_obj_set_style_pad_column(actions, settings_style::kButtonGap, 0);
  lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
  struct Action {
    const char* text;
    const char* icon;
    SystemAction action;
  };
  const Action kActions[] = {{s.settings_setup, "rocket-launch-outline", SystemAction::Setup},
                             {s.restart_button, "restart", SystemAction::Restart},
                             {settings_style::kGitHub, "github", SystemAction::GitHub}};
  for (const Action& a : kActions) {
    lv_obj_t* b = settings_parts::button(actions, a.text, a.icon, ButtonKind::Normal, settings_style::kSystemColor,
                                         palette, settings_style::kSystemButtonHeight, false, on_system_action,
                                         action_data(a.action));
    lv_obj_set_flex_grow(b, 1);
    // The first-start setup follows after the Settings pages.
    if (a.action == SystemAction::Setup) settings_parts::button_set_enabled(b, false);
  }

  // The head takes what the card has left, so the buttons end one card
  // margin above its edge (mockup growSystemHead).
  lv_obj_update_layout(page);
  lv_area_t page_area;
  lv_area_t last;
  lv_obj_get_coords(page, &page_area);
  lv_obj_get_coords(actions, &last);
  const int free = page_area.y2 - last.y2;
  if (free > 0) lv_obj_set_height(head_row, head_height + free);
}

void on_system_timer(lv_timer_t*) { system_tick(); }

void system_tick() {
  if (g_category != Category::System || !g_page_built || !g_page) return;
  const SystemValues v = settings_model::system_values();
  if (system_key(v) != g_system_key) {
    build_page();
    refresh_lines();
  } else {
    set_countdown(g_pair_time, v.pair_seconds);
    set_countdown(g_password_time, v.password_seconds);
    set_progress(v.progress);
    char address[48];
    char sub[64];
    if (g_address_label && settings_model::panel_address(address, sizeof(address))) {
      snprintf(sub, sizeof(sub), settings_model::text().settings_ip_fmt, address);
      if (strcmp(lv_label_get_text(g_address_label), sub) != 0) lv_label_set_text(g_address_label, sub);
    }
  }
  sync_dialog(v);
}

// ---------- Frame ----------

// The open page in the card; WiFi still uses its popup.
void build_page() {
  if (!g_page) return;
  clear_display_refs();
  clear_locale_refs();
  clear_system_refs();
  if (g_category != Category::System) {
    close_dialog();
    stop_system_timer();
  }
  lv_obj_clean(g_page);
  if (g_category == Category::Display) build_display_page(g_page);
  if (g_category == Category::Localization) build_localization_page(g_page);
  if (g_category == Category::System) {
    build_system_page(g_page);
    if (!g_system_timer) g_system_timer = lv_timer_create(on_system_timer, 500, nullptr);
  }
  g_page_built = true;
}

void clear_refs() {
  for (CategoryView& view : g_views) view = {};
  g_bar_title = nullptr;
  g_card_title = nullptr;
  g_page = nullptr;
  g_page_built = false;
  close_dialog();
  clear_display_refs();
  clear_locale_refs();
  clear_system_refs();
}

// Bar, categories and the empty card, in the colors of the moment.
void build_frame() {
  lv_obj_t* panel = g_panel;
  if (!panel) return;
  clear_refs();
  lv_obj_clean(panel);
  g_built_card = settings_model::card_color() & 0xFFFFFF;
  g_built_glow = ui_surface_style::icon_glow_percent();
  g_built_text = &settings_model::text();
  const Colors palette = colors();
  const i18n::Strings& s = settings_model::text();

  // Bar: the half-height tiles' circle, centered between the screen's top
  // edge and the tiles below (as much space above as below, user
  // 2026-10-06), and as far from the side edges as from the top, so gear
  // and X sit evenly in their corners. Positions are relative to the panel's
  // padding (the grid margins).
  const int inset = settings_style::tile_inset();
  const int d = settings_style::half_tile_disc();
  const int grid_width = settings_style::grid_w(0, GRID_COLS);
  const int edge = (GRID_PAD_TOP + settings_style::grid_y(0.5f) - d) / 2;
  const int bar_y = edge - GRID_PAD_TOP;
  const int gear_x = edge - GRID_PAD_LEFT;
  const int close_x = SCREEN_WIDTH - edge - d - GRID_PAD_LEFT;
  lv_obj_t* gear = circle(panel, gear_x, bar_y, d, "cog", 0xFFFFFF);
  lv_obj_set_style_bg_color(gear, lv_color_hex(settings_style::tone(palette.card, settings_style::kGearColor).disc),
                            0);
  lv_obj_set_style_bg_opa(gear, LV_OPA_COVER, 0);
  lv_obj_t* close = circle(panel, close_x, bar_y, d, "window-close", 0xFFFFFF);
  make_round_button(close, on_close_clicked, nullptr);
  int title_end = close_x;
  if (kLayout == Layout::Tabs) {
    // The tabs sit beside the X, so a longer or shorter page name never
    // moves them; a fifth of a circle between them.
    const int gap = (2 * d + 5) / 10;
    const int right = close_x - (settings_style::kBarGap + 2) / 4 - (d + 2) / 4;
    const int left = right - (kCategoryCount * d + (kCategoryCount - 1) * gap);
    for (uint8_t i = 0; i < kCategoryCount; ++i) {
      CategoryView& view = g_views[i];
      view.box = circle(panel, left + i * (d + gap), bar_y, d, category_icon(static_cast<Category>(i)), 0xFFFFFF);
      view.icon = lv_obj_get_child(view.box, 0);
      make_round_button(view.box, on_category_clicked, reinterpret_cast<void*>(static_cast<uintptr_t>(i)));
    }
    title_end = left;
  }
  const int title_x = gear_x + d + 2 * inset;
  g_bar_title = lv_label_create(panel);
  lv_label_set_text(g_bar_title, kLayout == Layout::Tabs ? category_title(g_category) : s.tile_type_settings);
  lv_label_set_long_mode(g_bar_title, LV_LABEL_LONG_DOT);
  lv_obj_set_width(g_bar_title, title_end - settings_style::kBarGap - title_x);
  lv_obj_set_style_text_font(g_bar_title, settings_style::bar_title_font(), 0);
  lv_obj_set_style_text_color(g_bar_title, lv_color_white(), 0);
  const float title_box = settings_parts::browser_line_height(settings_style::kRowFontPx);
  lv_obj_set_pos(g_bar_title, title_x,
                 settings_parts::browser_label_y(settings_style::bar_title_font(), settings_style::kRowFontPx,
                                                 bar_y + d / 2.0f - title_box / 2, title_box));

  // Categories and card below the bar, down to the grid's bottom edge.
  const int top = settings_style::grid_y(0.5f);
  const int bottom = settings_style::grid_y(GRID_ROWS) - GRID_GAP;
  if (kLayout == Layout::Split) {
    // The Settings exception: the tiles share the column height evenly.
    const float tile_h = (bottom - top - (kCategoryCount - 1) * GRID_GAP) / static_cast<float>(kCategoryCount);
    for (uint8_t i = 0; i < kCategoryCount; ++i) {
      build_category_tile(panel, i, 0, static_cast<int>(lroundf(top + i * (tile_h + GRID_GAP))),
                          settings_style::grid_w(0, 2), static_cast<int>(lroundf(tile_h)));
    }
  } else if (kLayout == Layout::Portrait) {
    const float half = GRID_COLS / 2.0f;
    for (uint8_t i = 0; i < kCategoryCount; ++i) {
      const float col = (i % 2) * half;
      const float row = GRID_ROWS - 2 + i / 2;
      build_category_tile(panel, i, settings_style::grid_x(col), settings_style::grid_y(row),
                          settings_style::grid_w(col, half), settings_style::grid_h(row, 1));
    }
  }
  int card_x = 0;
  int card_w = grid_width;
  int card_h = bottom - top;
  if (kLayout == Layout::Split) {
    card_x = settings_style::grid_x(2);
    card_w = settings_style::grid_w(2, GRID_COLS - 2);
  } else if (kLayout == Layout::Portrait) {
    card_h = settings_style::grid_h(0.5f, GRID_ROWS - 2.5f);
  }
  lv_obj_t* card = settings_parts::plain(panel);
  lv_obj_set_pos(card, card_x, top);
  lv_obj_set_size(card, card_w, card_h);
  lv_obj_set_style_bg_color(card, lv_color_hex(palette.card), 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  settings_style::apply_tile_radius(card);
  ui_surface_style::apply_global_tile_border(card);
  // The large page title where the card has room for it (five rows,
  // portrait); on square panels the bar names the page.
  const bool titled = kLayout == Layout::Portrait || GRID_ROWS >= 5;
  int body_top = settings_style::kCardPad;
  if (titled) {
    const int box = (settings_style::kTitleFontPx * 125 + 50) / 100;
    const int title_x2 = settings_style::kCardPad + settings_style::kSectionLeft;
    g_card_title = lv_label_create(card);
    lv_label_set_text(g_card_title, category_title(g_category));
    lv_label_set_long_mode(g_card_title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(g_card_title, card_w - title_x2 - settings_style::kCardPad);
    lv_obj_set_style_text_font(g_card_title, settings_style::page_title_font(), 0);
    lv_obj_set_style_text_color(g_card_title, lv_color_white(), 0);
    lv_obj_set_pos(g_card_title, title_x2,
                   settings_parts::browser_label_y(settings_style::page_title_font(), settings_style::kTitleFontPx,
                                                   settings_style::kCardPad + settings_style::kTitleTop, box));
    body_top = settings_style::kCardPad + settings_style::kTitleBodyTop;
  }
  g_page = settings_parts::plain(card);
  g_card_width = card_w;
  g_page_width = card_w - 2 * settings_style::kCardPad;
  lv_obj_set_pos(g_page, settings_style::kCardPad, body_top);
  lv_obj_set_size(g_page, g_page_width, card_h - body_top - settings_style::kCardPad);
  lv_obj_set_flex_flow(g_page, LV_FLEX_FLOW_COLUMN);
  // The first heading reaches a little above the page (browser_line).
  lv_obj_add_flag(g_page, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

  for (uint8_t i = 0; i < kCategoryCount; ++i) style_category(i);
  refresh_lines();
}

void select_category(Category category) {
  if (category == g_category && g_page_built) return;
  g_category = category;
  for (uint8_t i = 0; i < kCategoryCount; ++i) style_category(i);
  const char* title = category_title(category);
  if (kLayout == Layout::Tabs && g_bar_title) lv_label_set_text(g_bar_title, title);
  if (g_card_title) lv_label_set_text(g_card_title, title);
  build_page();
}

}  // namespace

void build(lv_obj_t* panel) {
  g_panel = panel;
  build_frame();
}

void prepare_show() {
  if (!g_panel) return;
  if ((settings_model::card_color() & 0xFFFFFF) != g_built_card ||
      ui_surface_style::icon_glow_percent() != g_built_glow || &settings_model::text() != g_built_text) {
    build_frame();
  }
  refresh_lines();
  build_page();
  if (!g_lines_timer) g_lines_timer = lv_timer_create(on_lines_timer, 5000, nullptr);
}

void did_hide() {
  if (g_lines_timer) {
    lv_timer_delete(g_lines_timer);
    g_lines_timer = nullptr;
  }
  close_dialog();
  stop_system_timer();
  clear_display_refs();
  clear_locale_refs();
  clear_system_refs();
  if (g_page) lv_obj_clean(g_page);
  g_page_built = false;
}

void refresh_lines() {
  for (uint8_t i = 0; i < kCategoryCount; ++i) {
    const CategoryView& view = g_views[i];
    const Category category = static_cast<Category>(i);
    if (view.icon) {
      const auto icon = getMdiChar(category_icon(category));
      if (strcmp(lv_label_get_text(view.icon), icon.c_str()) != 0) lv_label_set_text(view.icon, icon.c_str());
    }
    if (!view.line) continue;
    char buf[96];
    bool warn = false;
    category_line(category, buf, sizeof(buf), &warn);
    if (strcmp(lv_label_get_text(view.line), buf) != 0) lv_label_set_text(view.line, buf);
    lv_obj_set_style_text_color(view.line, warn ? lv_color_hex(0xFFC04D) : lv_color_white(), 0);
    lv_obj_set_style_text_opa(view.line, warn ? LV_OPA_COVER : settings_style::kGreyOpa, 0);
  }
}

void sync_rotation() { settings_parts::segment_select(g_rotation_segment, settings_model::rotation_index()); }

void texts_changed() {
  g_built_text = nullptr;
  if (g_panel && !lv_obj_has_flag(g_panel, LV_OBJ_FLAG_HIDDEN)) prepare_show();
}

void show_category(uint8_t index) {
  if (index < kCategoryCount) select_category(static_cast<Category>(index));
}

void system_changed() { system_tick(); }

}  // namespace settings_screen
