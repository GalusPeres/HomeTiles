#include "src/ui/tabs/settings/settings_screen.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "src/tiles/icons/mdi_icons.h"
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

// A category tile or tab. Display opens its page in the card; the others
// still open their popups until their pages follow.
void on_category_clicked(lv_event_t* e) {
  const uintptr_t raw = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
  if (raw >= kCategoryCount) return;
  const Category category = static_cast<Category>(raw);
  if (category != Category::Display) {
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

// ---------- Frame ----------

// The open page in the card; only Display has its page so far.
void build_page() {
  if (!g_page) return;
  clear_display_refs();
  lv_obj_clean(g_page);
  if (g_category == Category::Display) build_display_page(g_page);
  g_page_built = true;
}

void clear_refs() {
  for (CategoryView& view : g_views) view = {};
  g_bar_title = nullptr;
  g_card_title = nullptr;
  g_page = nullptr;
  g_page_built = false;
  clear_display_refs();
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
  clear_display_refs();
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

}  // namespace settings_screen
