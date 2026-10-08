#include "src/ui/tabs/tiles/home_bar.h"

#include <Arduino.h>
#include <string.h>
#include <time.h>

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/fonts/ui_fonts.h"
#include "src/ui/shared/icon_lock_mark.h"
#include "src/tiles/config/grid_layout.h"
#include "src/tiles/config/tile_config.h"
#include "src/tiles/icons/mdi_icons.h"
#include "src/types/clock/clock_format.h"
#include "src/ui/popups/popup_layout.h"
#include "src/ui/tabs/settings/settings_style.h"
#include "src/ui/ui_manager.h"

// grid_layout.h sizes the bar's grid with the head's frame; it must be the
// popups' (and the Settings head's).
static_assert(grid_layout::kHeadMargin == popup_layout::kCardMargin, "head bar margin");
static_assert(grid_layout::kHeadClose == popup_layout::kCloseButtonSize, "head bar X box");

namespace home_bar {
namespace {

// Its address marks the bar among a grid's children (lv_obj user data).
int g_marker = 0;

// The time labels of every built bar (one per cached grid); the timer keeps
// them current, a deleted bar takes its label out.
constexpr size_t kMaxTimeLabels = 12;
lv_obj_t* g_time_labels[kMaxTimeLabels] = {};
lv_timer_t* g_time_timer = nullptr;
char g_time_text[8] = "";

void refresh_time(bool force) {
  char text[sizeof(g_time_text)];
  format_time(text, sizeof(text));
  if (!force && strcmp(text, g_time_text) == 0) return;
  memcpy(g_time_text, text, sizeof(g_time_text));
  for (lv_obj_t* label : g_time_labels) {
    if (label) lv_label_set_text(label, g_time_text);
  }
}

void time_tick(lv_timer_t*) { refresh_time(false); }

void forget_time_label(lv_event_t* event) {
  lv_obj_t* label = static_cast<lv_obj_t*>(lv_event_get_target(event));
  for (lv_obj_t*& slot : g_time_labels) {
    if (slot == label) slot = nullptr;
  }
}

void remember_time_label(lv_obj_t* label) {
  for (lv_obj_t*& slot : g_time_labels) {
    if (slot) continue;
    slot = label;
    lv_obj_add_event_cb(label, forget_time_label, LV_EVENT_DELETE, nullptr);
    return;
  }
  // More bars than slots: this one keeps the time it was built with.
  Serial.println("[HomeBar] No free time label slot");
}

int text_width(const lv_font_t* font, const char* text) {
  lv_point_t size{};
  lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  return size.x;
}

// The largest of the head fonts that fits the title in one line (like the
// Settings head title); 14 and 12 exist only on the 480x480 panels.
const lv_font_t* title_font(const char* text, int width) {
  const lv_font_t* const fonts[] = {head_font(), settings_style::small_font(),
#if defined(DEVICE_LAYOUT_480X480)
                                    &ui_font_14, &ui_font_12,
#else
                                    &ui_font_16,
#endif
  };
  for (const lv_font_t* candidate : fonts) {
    if (text_width(candidate, text) <= width) return candidate;
  }
  return fonts[sizeof(fonts) / sizeof(fonts[0]) - 1];
}

void on_gear(lv_event_t*) { uiManager.requestSettingsAccess(); }

// A Settings PIN: the gear shows the lock like the Settings tile
// (icon_lock_mark.h), its rim in the color behind the head; a pressed gear's
// light fill mixed over it, like the tile's disc.
void gear_lock_event_cb(lv_event_t* event) {
  lv_obj_t* icon = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
  if (lv_event_get_code(event) == LV_EVENT_REFR_EXT_DRAW_SIZE) {
    icon_lock_mark::ext_draw_size(event, icon);
    return;
  }
  if (lv_event_get_code(event) != LV_EVENT_DRAW_POST) return;
  lv_obj_t* button = lv_obj_get_parent(icon);
  lv_color_t under = lv_color_black();
  for (lv_obj_t* obj = button ? lv_obj_get_parent(button) : nullptr; obj; obj = lv_obj_get_parent(obj)) {
    if (lv_obj_get_style_bg_opa(obj, LV_PART_MAIN) <= LV_OPA_MIN) continue;
    under = lv_obj_get_style_bg_color(obj, LV_PART_MAIN);
    break;
  }
  icon_lock_mark::draw(lv_event_get_layer(event), icon, icon_lock_mark::behind(button, under));
}

void on_back(lv_event_t* event) {
  const uint16_t folder_id = static_cast<uint16_t>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
  uiManager.switchToFolder(tileConfig.getFolderParent(folder_id));
}

lv_obj_t* plain(lv_obj_t* parent) {
  lv_obj_t* obj = lv_obj_create(parent);
  lv_obj_remove_style_all(obj);
  lv_obj_remove_flag(obj, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  return obj;
}

}  // namespace

// HH:MM in the panel's time format (12 hours without AM/PM, like a clock
// face); dashes until the clock is set.
void format_time(char* out, size_t size) {
  const time_t now = time(nullptr);
  struct tm local {};
  if (now < 1704067200 || !localtime_r(&now, &local)) {
    snprintf(out, size, "--:--");
    return;
  }
  const DeviceConfig& config = configManager.getConfig();
  const uint8_t format = clock_tile::resolve_time_format(clock_tile::TIME_FORMAT_AUTO, config.global_time_format,
                                                          config.language);
  int hour = local.tm_hour;
  if (format == clock_tile::TIME_FORMAT_12H) {
    hour %= 12;
    if (hour == 0) hour = 12;
  }
  snprintf(out, size, "%02d:%02d", hour, local.tm_min);
}

const lv_font_t* head_font() { return popup_layout::headerTitleFont(); }

const lv_font_t* time_font() {
#if defined(DEVICE_LAYOUT_1024X600) || defined(DEVICE_LAYOUT_480X480)
  return &ui_font_20;
#else
  return &ui_font_32;
#endif
}

Geometry geometry() {
  // The Settings head's places: the X in the top right corner one grid gap
  // inside the card margin, the circle mirroring it on the left, everything
  // on the X's centre line; the time right before the X, as wide as the
  // widest time in its font (its digits are not all equally wide), the
  // title between the circle and the time.
  Geometry g{};
  g.disc = popup_layout::kHeaderIconDiscSize;
  g.close = popup_layout::kCloseButtonSize;
  g.center_y = settings_style::head_center_y();
  g.disc_x = settings_style::head_disc_x();
  g.close_x = settings_style::inner_right() - g.close;
  const int space = g.disc / 5;
  const lv_font_t* font = time_font();
  int digit_w = 0;
  for (char digit = '0'; digit <= '9'; ++digit) {
    const char text[2] = {digit, 0};
    const int width = text_width(font, text);
    if (width > digit_w) digit_w = width;
  }
  g.time_w = 4 * digit_w + text_width(font, ":");
  g.time_x = g.close_x - space - g.time_w;
  g.title_x = g.disc_x + g.disc + popup_layout::kHeaderIconDiscGap;
  g.title_w = g.time_x - space - g.title_x;
  if (g.title_w < 1) g.title_w = 1;
  g.time_x_alone = g.close_x + g.close - g.time_w;
  g.title_w_alone = g.title_w + g.time_x_alone - g.time_x;
  g.line = lv_font_get_line_height(head_font());
  g.time_line = lv_font_get_line_height(font);
  return g;
}

bool is_bar(const lv_obj_t* obj) { return obj && lv_obj_get_user_data(const_cast<lv_obj_t*>(obj)) == &g_marker; }

void build(lv_obj_t* grid, uint16_t folder_id) {
  if (!grid || !grid_layout::head_bar()) return;
  const bool home = folder_id == 0;  // the root folder (TileConfig)
  const DeviceConfig& config = configManager.getConfig();
  const i18n::Strings& tr = i18n::strings(config.language);
  const Geometry g = geometry();

  // One floating object over the grid's top margin, in screen coordinates:
  // the tile grid's layout and its incremental updates leave it alone.
  lv_obj_t* bar = plain(grid);
  lv_obj_add_flag(bar, LV_OBJ_FLAG_FLOATING);
  lv_obj_set_user_data(bar, &g_marker);
  lv_obj_set_pos(bar, -GRID_PAD_LEFT, -GRID_PAD_TOP);
  lv_obj_set_size(bar, SCREEN_WIDTH, GRID_PAD_TOP);

  // The page's circle: Home's house or the folder's icon, in the Settings
  // gear's grey over the global tile color.
  const FolderEntry* folder = home ? nullptr : tileConfig.getFolder(folder_id);
  const char* icon_name = home ? "home" : (folder && folder->icon_name[0] ? folder->icon_name : "folder");
  const settings_style::Tone tone = settings_style::tone(tileDefaultBgColor(), settings_style::kGearColor);
  lv_obj_t* circle = plain(bar);
  lv_obj_set_pos(circle, g.disc_x, g.center_y - g.disc / 2);
  lv_obj_set_size(circle, g.disc, g.disc);
  lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(circle, lv_color_hex(tone.disc), 0);
  lv_obj_set_style_bg_opa(circle, LV_OPA_COVER, 0);
  lv_obj_t* icon = lv_label_create(circle);
  lv_label_set_text(icon, getMdiChar(normalizeMdiIconName(icon_name)).c_str());
  if (FONT_MDI_ICONS) lv_obj_set_style_text_font(icon, FONT_MDI_ICONS, 0);
  lv_obj_set_style_text_color(icon, lv_color_hex(tone.icon), 0);
  lv_obj_center(icon);

  // The gear on Home (when chosen in the Settings tile's options), the X in a
  // folder: the Settings X's pressed shape and corner, the touch area keeps
  // the X's box.
  const int pressed = settings_style::kClosePressed;
  lv_obj_t* button = plain(bar);
  const bool gear_hidden = home && !configManager.getConfig().head_gear;
  if (gear_hidden) lv_obj_add_flag(button, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_pos(button, g.close_x + (g.close - pressed) / 2, g.center_y - pressed / 2);
  lv_obj_set_size(button, pressed, pressed);
  ui_surface_style::apply_radius(button, settings_style::kClosePressedBaseline);
  lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(button, lv_color_white(), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(button, LV_OPA_20, LV_STATE_PRESSED);
  lv_obj_set_ext_click_area(button, popup_layout::kCloseButtonClickArea + (g.close - pressed) / 2);
  lv_obj_t* glyph = lv_label_create(button);
  lv_label_set_text(glyph, getMdiChar(home ? "cog" : "window-close").c_str());
  if (FONT_MDI_ICONS) lv_obj_set_style_text_font(glyph, FONT_MDI_ICONS, 0);
  lv_obj_set_style_text_color(glyph, lv_color_white(), 0);
  lv_obj_center(glyph);
  if (home) {
    lv_obj_add_event_cb(button, on_gear, LV_EVENT_CLICKED, nullptr);
    if (configManager.getConfig().settings_pin_enabled) {
      lv_obj_add_event_cb(glyph, gear_lock_event_cb, LV_EVENT_DRAW_POST, nullptr);
      lv_obj_add_event_cb(glyph, gear_lock_event_cb, LV_EVENT_REFR_EXT_DRAW_SIZE, nullptr);
      lv_obj_refresh_ext_draw_size(glyph);
    }
  } else {
    lv_obj_add_event_cb(button, on_back, LV_EVENT_CLICKED,
                        reinterpret_cast<void*>(static_cast<uintptr_t>(folder_id)));
  }

  // The time right before the X; without the gear in its place.
  lv_obj_t* time_label = lv_label_create(bar);
  lv_obj_set_style_text_font(time_label, time_font(), 0);
  lv_obj_set_style_text_color(time_label, lv_color_white(), 0);
  lv_obj_set_style_text_align(time_label, LV_TEXT_ALIGN_RIGHT, 0);
  lv_label_set_long_mode(time_label, LV_LABEL_LONG_CLIP);
  lv_obj_set_size(time_label, g.time_w, g.time_line);
  lv_obj_set_pos(time_label, gear_hidden ? g.time_x_alone : g.time_x, g.center_y - g.time_line / 2);
  if (!g_time_text[0]) format_time(g_time_text, sizeof(g_time_text));
  lv_label_set_text(time_label, g_time_text);
  remember_time_label(time_label);
  if (!g_time_timer) g_time_timer = lv_timer_create(time_tick, 5000, nullptr);

  // Home or the folder's name, one line between the circle and the time.
  const char* title_text = home ? tr.home : (folder && folder->name[0] ? folder->name : tr.home);
  const int title_w = gear_hidden ? g.title_w_alone : g.title_w;
  const lv_font_t* font = title_font(title_text, title_w);
  lv_obj_t* title = lv_label_create(bar);
  lv_obj_set_style_text_font(title, font, 0);
  lv_obj_set_style_text_color(title, lv_color_white(), 0);
  lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
  lv_obj_set_size(title, title_w, lv_font_get_line_height(font));
  lv_label_set_text(title, title_text);
  lv_obj_set_pos(title, g.title_x, g.center_y - lv_font_get_line_height(font) / 2);
}

}  // namespace home_bar
