#include "src/ui/tabs/tiles/home_bar.h"

#include <Arduino.h>
#include <math.h>
#include <string.h>
#include <time.h>

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/fonts/ui_fonts.h"
#include "src/ui/shared/icon_lock_mark.h"
#include "src/tiles/config/grid_layout.h"
#include "src/tiles/config/tile_config.h"
#include "src/tiles/config/tile_layouts.h"
#include "src/tiles/icons/mdi_icons.h"
#include "src/types/clock/clock_format.h"
#include "src/ui/popups/popup_layout.h"
#include "src/ui/tabs/tiles/page_preview.h"
#include "src/ui/tabs/settings/settings_style.h"
#include "src/ui/ui_manager.h"
#include "src/ui/shared/ui_theme.h"
#include "src/ui/shared/tabular_digits.h"
#include "src/ui/shared/touch_rules.h"
#include "src/ui/timers/timer_ui.h"
#include "src/video/local_camera/local_camera.h"

// grid_layout.h sizes the bar's grid with the head's frame; it must be the
// popups' (and the Settings head's).
static_assert(grid_layout::kHeadMargin == popup_layout::kCardMargin, "head bar margin");
static_assert(grid_layout::kHeadClose == popup_layout::kCloseButtonSize, "head bar X box");

namespace home_bar {
namespace {

// Its address marks the bar among a grid's children (lv_obj user data).
int g_marker = 0;
// And the page dots' row inside a bar, and its gear or X.
int g_dots_marker = 0;
int g_button_marker = 0;

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
  lv_color_t under = lv_color_hex(ui_theme::screen());
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

// The head's base: the screen in the light theme (the head sits on it, not
// on a card), the tile card in the dark one.
uint32_t head_base() { return ui_theme::light() ? ui_theme::screen() : tileDefaultBgColor(); }

// The page circle's colors (the head's Home or folder circle, and the camera
// circle beside it): the gear's grey one step from the head's base.
settings_style::Tone page_circle_tone() { return settings_style::tone(head_base(), settings_style::kGearColor); }

lv_obj_t* find_dots(lv_obj_t* grid) {
  if (!grid) return nullptr;
  const uint32_t count = lv_obj_get_child_count(grid);
  for (uint32_t i = 0; i < count; ++i) {
    lv_obj_t* bar = lv_obj_get_child(grid, static_cast<int32_t>(i));
    if (!is_bar(bar)) continue;
    const uint32_t parts = lv_obj_get_child_count(bar);
    for (uint32_t j = 0; j < parts; ++j) {
      lv_obj_t* part = lv_obj_get_child(bar, static_cast<int32_t>(j));
      if (lv_obj_get_user_data(part) == &g_dots_marker) return part;
    }
  }
  return nullptr;
}

// The line over the dots (user 2026-10-12: it follows the finger and joins
// the dots fluidly; mockup build/design-mockups/pages/dots-swipe.html). The
// grey dots stand still; the line in the text color goes from page i's
// place to page j's, its leading edge ahead, the trailing one following,
// and fades the dots it covers out and the ones it leaves in (no grey
// slivers at its round ends). The row's last child is the line.
int rest_x(int k, int active) {
  const DotSizes d = dot_sizes();
  return k * (d.dot + d.gap) + (k > active ? d.active - d.dot : 0);
}
float ease_in(float t) { return t * t; }
float ease_out(float t) { return 1.0f - (1.0f - t) * (1.0f - t); }
float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

void lay_out_line(lv_obj_t* dots, int i, int j, float p, int stretch) {
  if (!dots) return;
  const uint32_t children = lv_obj_get_child_count(dots);
  if (children < 2) return;
  const int count = static_cast<int>(children) - 1;
  if (i < 0 || j < 0 || i >= count || j >= count) return;
  const DotSizes d = dot_sizes();
  const float l0 = static_cast<float>(rest_x(i, i));
  const float l1 = static_cast<float>(rest_x(j, j));
  float left = l0;
  float right = l0 + d.active;
  if (j > i) {
    left = l0 + (l1 - l0) * ease_in(p);
    right = l0 + d.active + (l1 - l0) * ease_out(p);
  } else if (j < i) {
    left = l0 + (l1 - l0) * ease_out(p);
    right = l0 + d.active + (l1 - l0) * ease_in(p);
  }
  if (stretch > 0) right += stretch;
  if (stretch < 0) left += stretch;
  for (int k = 0; k < count; ++k) {
    const float from_x = static_cast<float>(rest_x(k, i));
    const float to_x = static_cast<float>(rest_x(k, j));
    // Page i's dot where it ends up, page j's where it was, the others on
    // their way (only a jump over several pages moves them).
    const float x = k == i ? to_x : k == j ? from_x : from_x + (to_x - from_x) * p;
    float shown = 1;
    if (k == i && j == i) shown = 0;
    else if (k == i) shown = j > i ? clamp01((left - x) / d.dot) : clamp01((x + d.dot - right) / d.dot);
    else if (k == j) shown = j > i ? clamp01((x + d.dot - right) / d.dot) : clamp01((left - x) / d.dot);
    lv_obj_t* dot = lv_obj_get_child(dots, k);
    lv_obj_set_x(dot, static_cast<int32_t>(lroundf(x)));
    lv_obj_set_style_bg_opa(dot, static_cast<lv_opa_t>(lroundf(shown * LV_OPA_COVER)), 0);
  }
  lv_obj_t* line = lv_obj_get_child(dots, count);
  const int32_t l = static_cast<int32_t>(lroundf(left));
  lv_obj_set_x(line, l);
  lv_obj_set_width(line, static_cast<int32_t>(lroundf(right)) - l);
}

// The line's own way after the finger (180 ms): on to the new page, back to
// the old one, or back from a pull past the first or last page.
struct Motion {
  lv_obj_t* dots;
  int from;
  int to;
  float p0;
  float p1;
  int stretch;
};
Motion g_motion{};
void motion_step(void*, int32_t value) {
  const float t = static_cast<float>(value) / 1024.0f;
  lay_out_line(g_motion.dots, g_motion.from, g_motion.to, g_motion.p0 + (g_motion.p1 - g_motion.p0) * t,
               static_cast<int>(lroundf(static_cast<float>(g_motion.stretch) * (1.0f - t))));
}
void motion_done(lv_anim_t*) {
  const int page = g_motion.p1 >= 1.0f ? g_motion.to : g_motion.from;
  lay_out_line(g_motion.dots, page, page, 0, 0);
  g_motion = {};
}
void stop_motion() {
  lv_anim_delete(&g_motion, motion_step);
  g_motion = {};
}
void move_line(lv_obj_t* dots, int from, int to, float p0, float p1, int stretch, uint32_t duration) {
  stop_motion();
  g_motion = {dots, from, to, p0, p1, stretch};
  motion_step(nullptr, 0);
  lv_anim_t anim;
  lv_anim_init(&anim);
  lv_anim_set_var(&anim, &g_motion);
  lv_anim_set_exec_cb(&anim, motion_step);
  lv_anim_set_values(&anim, 0, 1024);
  lv_anim_set_duration(&anim, duration);
  lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
  lv_anim_set_completed_cb(&anim, motion_done);
  lv_anim_start(&anim);
}

// The head's dots (user 2026-10-13: one touch, one meaning, like the
// iPhone's page dots): while the finger stays within a tap's distance
// (touch_rules.h) it is a press on them; the pill shows once it rested
// there 100 ms (at once it showed at the start of every swipe). A tap opens
// the pages' overview when the finger lifts; a hold of 400 ms opens it under
// the finger, which then picks a page by sliding onto its picture and
// lifting there (page_preview.h). A finger that went further is a swipe:
// no pill, no overview; sideways it turns the page (ui_manager.cpp;
// cancel_press: the input then waits for the release, the row never hears
// it).
constexpr uint32_t kHoldOpenMs = 400;
lv_obj_t* g_press_dots = nullptr;
lv_point_t g_press_at{};
uint32_t g_press_ms = 0;
bool g_press_held = false;  // the overview opened under the finger
void release_press(lv_obj_t* dots) {
  if (dots) lv_obj_remove_state(dots, LV_STATE_USER_1);
  if (g_press_dots == dots) g_press_dots = nullptr;
  g_press_held = false;
}
void dots_event(lv_event_t* event) {
  lv_obj_t* dots = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
  const lv_event_code_t code = lv_event_get_code(event);
  lv_indev_t* input = lv_indev_active();
  lv_point_t now{};
  if (input) lv_indev_get_point(input, &now);
  if (code == LV_EVENT_PRESSED) {
    g_press_dots = dots;
    g_press_ms = lv_tick_get();
    g_press_held = false;
    g_press_at = now;
    return;
  }
  if (g_press_dots != dots) return;
  if (code == LV_EVENT_PRESSING) {
    if (!input) return;
    if (g_press_held) {
      page_preview::point_at(now);
      return;
    }
    const int32_t dx = now.x - g_press_at.x;
    const int32_t dy = now.y - g_press_at.y;
    const int32_t limit = touch_rules::kTapSlop;
    if (dx * dx + dy * dy > limit * limit) {
      // A swipe: the page turns when it goes sideways, nothing else.
      release_press(dots);
      return;
    }
    const uint32_t held = lv_tick_elaps(g_press_ms);
    if (held >= kHoldOpenMs) {
      // Under the finger: no page turn and no edge action any more.
      lv_obj_remove_state(dots, LV_STATE_USER_1);
      g_press_held = true;
      uiManager.cancelTouchGesture();
      if (!page_preview::is_open()) page_preview::open(dots);
    } else if (held >= touch_rules::kPressDelayMs && !lv_obj_has_state(dots, LV_STATE_USER_1)) {
      lv_obj_add_state(dots, LV_STATE_USER_1);
    }
    return;
  }
  if (code == LV_EVENT_RELEASED) {
    // Lifted after a hold: the picture under the finger, or the overview
    // stays open; after a tap the overview opens.
    if (g_press_held) {
      release_press(dots);
      page_preview::lift_at(now);
      return;
    }
    release_press(dots);
    if (!page_preview::is_open()) page_preview::open(dots);
    return;
  }
  // Lost (a swipe took over): no page lights up any more.
  if (g_press_held) page_preview::point_at({-1, -1});
  release_press(dots);
}
// Beside the dots' pill a tap turns one page to that side (user
// 2026-10-14, like the iPhone's page dots), as well as a swipe does.
void step_event(lv_event_t* event) {
  const int step = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
  const int page = tile_layouts::page_number(tileConfig.getActiveFolderId());
  if (page >= 0) uiManager.showPage(page + step);
}

// The row is gone: nothing may reach it any more.
void forget_dots(lv_event_t* event) {
  lv_obj_t* dots = static_cast<lv_obj_t*>(lv_event_get_target(event));
  if (g_motion.dots == dots) stop_motion();
  if (g_press_dots == dots) g_press_dots = nullptr;
}

}  // namespace

// Sized by the screen's short side like the mockup (12, the active one 34,
// on an 800 px side).
DotSizes dot_sizes() {
  const int side = grid_layout::screen_w() < grid_layout::screen_h() ? grid_layout::screen_w() : grid_layout::screen_h();
  const int dot = (12 * side + 400) / 800;
  return {dot, (34 * side + 400) / 800, dot};
}

uint32_t dot_color(bool active) {
  if (active) return ui_theme::text();
  return ui_theme::light() ? ui_theme::kLight.icon_unavailable : 0x5C5C5C;
}

const char* home_title(const char* fallback) {
  const char* own = configManager.getConfig().head_title;
  return own[0] ? own : fallback;
}

const char* home_icon() {
  const char* own = configManager.getConfig().head_icon;
  return own[0] ? own : "home";
}

int step_zone_width() { return 2 * popup_layout::kHeaderIconDiscSize; }

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

// Fixed-width digits like the clock tile (tabular_digits).
const lv_font_t* time_font() {
#if defined(DEVICE_LAYOUT_1024X600) || defined(DEVICE_LAYOUT_480X480)
  return tabular_digits::font(&ui_font_20);
#else
  return tabular_digits::font(&ui_font_32);
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
  g.line = lv_font_get_line_height(head_font());
  g.time_line = lv_font_get_line_height(font);
  // Without the gear the time moves right until the digits are as far from
  // the screen's right edge as from its top (user 2026-10-08), measured on
  // the glyphs: the label box has the font's spacing above and beside them.
  lv_font_glyph_dsc_t digit{};
  lv_font_get_glyph_dsc(font, &digit, '0', 0);
  const int ink_top = g.time_line - font->base_line - digit.box_h - digit.ofs_y;
  const int top_gap = g.center_y - g.time_line / 2 + ink_top;
  const int right_bearing = digit.adv_w - digit.ofs_x - digit.box_w;
  g.time_x_alone = grid_layout::screen_w() - top_gap + right_bearing - g.time_w;
  if (g.time_x_alone < g.time_x) g.time_x_alone = g.time_x;
  g.title_w_alone = g.title_w + g.time_x_alone - g.time_x;
  return g;
}

bool is_bar(const lv_obj_t* obj) { return obj && lv_obj_get_user_data(const_cast<lv_obj_t*>(obj)) == &g_marker; }

bool is_button(const lv_obj_t* obj) {
  for (lv_obj_t* o = const_cast<lv_obj_t*>(obj); o && !is_bar(o); o = lv_obj_get_parent(o)) {
    if (lv_obj_get_user_data(o) == &g_button_marker) return true;
  }
  return false;
}

static void add_camera_circle(lv_obj_t* bar, const Geometry& g, int right);

void build(lv_obj_t* grid, uint16_t folder_id) {
  if (!grid || !grid_layout::head_bar()) return;
  const bool home = folder_id == 0;  // the root folder (TileConfig)
  // Home and its pages (tile_layouts::pages) are one level: the dots in the
  // middle, the gear and the time on the right; a folder has its X instead.
  const int page = tile_layouts::page_number(folder_id);
  const bool home_level = home || page > 0;
  const DeviceConfig& config = configManager.getConfig();
  const i18n::Strings& tr = i18n::strings(config.language);
  const Geometry g = geometry();

  // One floating object over the grid's top margin, in screen coordinates:
  // the tile grid's layout and its incremental updates leave it alone.
  lv_obj_t* bar = plain(grid);
  lv_obj_add_flag(bar, LV_OBJ_FLAG_FLOATING);
  lv_obj_set_user_data(bar, &g_marker);
  lv_obj_set_pos(bar, -GRID_PAD_LEFT, -GRID_PAD_TOP);
  lv_obj_set_size(bar, grid_layout::screen_w(), GRID_PAD_TOP);

  // The page's circle: Home's icon (the house unless chosen) or the
  // folder's, in the Settings gear's grey over the global tile color.
  const FolderEntry* folder = home ? nullptr : tileConfig.getFolder(folder_id);
  const char* icon_name = home ? home_icon() : (folder && folder->icon_name[0] ? folder->icon_name : "folder");
  // The light theme's head sits on the screen, not on a card: its circle is
  // one step from the screen like the dark theme's circle from its card.
  const settings_style::Tone tone = page_circle_tone();
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
  const bool gear_hidden = home_level && !configManager.getConfig().head_gear;
  if (gear_hidden) lv_obj_add_flag(button, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_pos(button, g.close_x + (g.close - pressed) / 2, g.center_y - pressed / 2);
  lv_obj_set_size(button, pressed, pressed);
  ui_surface_style::apply_radius(button, settings_style::kClosePressedBaseline);
  lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_user_data(button, &g_button_marker);
  lv_obj_set_style_bg_color(button, lv_color_hex(ui_theme::icon()), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(button, LV_OPA_20, LV_STATE_PRESSED);
  lv_obj_set_ext_click_area(button, popup_layout::kCloseButtonClickArea + (g.close - pressed) / 2);
  lv_obj_t* glyph = lv_label_create(button);
  lv_label_set_text(glyph, getMdiChar(home_level ? "cog" : "window-close").c_str());
  if (FONT_MDI_ICONS) lv_obj_set_style_text_font(glyph, FONT_MDI_ICONS, 0);
  lv_obj_set_style_text_color(glyph, lv_color_hex(ui_theme::text()), 0);
  lv_obj_center(glyph);
  if (home_level) {
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
  lv_obj_set_style_text_color(time_label, lv_color_hex(ui_theme::text()), 0);
  lv_obj_set_style_text_align(time_label, LV_TEXT_ALIGN_RIGHT, 0);
  lv_label_set_long_mode(time_label, LV_LABEL_LONG_CLIP);
  lv_obj_set_size(time_label, g.time_w, g.time_line);
  lv_obj_set_pos(time_label, gear_hidden ? g.time_x_alone : g.time_x, g.center_y - g.time_line / 2);
  if (!g_time_text[0]) format_time(g_time_text, sizeof(g_time_text));
  lv_label_set_text(time_label, g_time_text);
  remember_time_label(time_label);
  // Without the time (Global tab "Kopfleiste", user 2026-10-13) its place
  // stays empty.
  if (!config.head_clock) lv_obj_add_flag(time_label, LV_OBJ_FLAG_HIDDEN);
  if (!g_time_timer) g_time_timer = lv_timer_create(time_tick, 5000, nullptr);
  // A voice timer's pill right before the time (timer_ui.h), in the time's
  // place without it; the camera circle and the title make room. A tap on
  // the time opens the Timers popup too, for its presets.
  const int time_left = gear_hidden ? g.time_x_alone : g.time_x;
  const int pill_right = config.head_clock ? time_left - g.disc / 5 : time_left + g.time_w;
  const int pill_left = timer_ui::add_head_pill(bar, pill_right, g.center_y, g.disc, head_base());
  const bool pill = pill_left != pill_right;
  if (config.head_clock && timer_ui::head_time_opens_popup()) {
    lv_obj_add_flag(time_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(time_label, (g.disc - g.time_line) / 2);
    lv_obj_add_event_cb(time_label, [](lv_event_t*) { timer_ui::open_popup(); }, LV_EVENT_CLICKED, nullptr);
  }
  add_camera_circle(bar, g, (pill ? pill_left : time_left) - g.disc / 5);

  // The dots in the middle of the head, Home's first.
  int title_end = g.title_x + (gear_hidden ? g.title_w_alone : g.title_w);
  if (page >= 0) {
    uint16_t pages[tile_layouts::kMaxPages];
    const int count = static_cast<int>(tile_layouts::pages(pages)) + 1;
    const DotSizes d = dot_sizes();
    const int row_x = (grid_layout::screen_w() - dots_width(count)) / 2;
    // A press shows a pill around the row as high as the head's circle and
    // as much beside (user 2026-10-12, mockup
    // build/design-mockups/pages/dots-swipe.html) in a light fill.
    const int side = grid_layout::screen_w() < grid_layout::screen_h() ? grid_layout::screen_w() : grid_layout::screen_h();
    const int pad = (g.disc - d.dot) / 2;
    const int box_h = d.dot + 2 * pad;
    lv_obj_t* dots = create_dots(bar, count, page, row_x - pad, g.center_y - box_h / 2);
    lv_obj_set_size(dots, dots_width(count) + 2 * pad, box_h);
    lv_obj_set_style_pad_all(dots, pad, 0);
    lv_obj_set_style_radius(dots, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dots, lv_color_hex(ui_theme::icon()), LV_STATE_USER_1);
    lv_obj_set_style_bg_opa(dots, 31, LV_STATE_USER_1);
    lv_obj_set_user_data(dots, &g_dots_marker);
    // Without the pages' overview (Global tab "Kopfleiste", user
    // 2026-10-13) the dots only show the page: no press, no overview; a
    // swipe on them still turns the page.
    if (config.head_overview) {
      // A tap reaches them 20 px around the pill on an 800 px side (user
      // 2026-10-12: one had to hit them exactly).
      lv_obj_add_flag(dots, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_ext_click_area(dots, (20 * side + 400) / 800);
      lv_obj_add_event_cb(dots, dots_event, LV_EVENT_PRESSED, nullptr);
      lv_obj_add_event_cb(dots, dots_event, LV_EVENT_PRESSING, nullptr);
      lv_obj_add_event_cb(dots, dots_event, LV_EVENT_RELEASED, nullptr);
      lv_obj_add_event_cb(dots, dots_event, LV_EVENT_PRESS_LOST, nullptr);
      // A drag down onto the overview's pictures never scrolls a parent.
      lv_obj_remove_flag(dots, LV_OBJ_FLAG_SCROLL_CHAIN);
    }
    // A tap beside the pill turns one page to that side (step_event): over
    // the whole head, the title or time under it included, never over the
    // gear's touch area; Home has none on its left, the last page none on its
    // right. A tap on the dots themselves opens the overview as before.
    const int gear_left = gear_hidden ? grid_layout::screen_w() : g.close_x - popup_layout::kCloseButtonClickArea;
    for (const int step : {-1, 1}) {
      if (page + step < 0 || page + step >= count) continue;
      const int x = step < 0 ? row_x - pad - step_zone_width() : row_x + dots_width(count) + pad;
      const int w = step < 0 ? step_zone_width() : LV_MIN(step_zone_width(), gear_left - x);
      if (w <= 0) continue;
      lv_obj_t* zone = plain(bar);
      lv_obj_set_size(zone, w, GRID_PAD_TOP);
      lv_obj_set_pos(zone, x, 0);
      lv_obj_add_flag(zone, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_add_event_cb(zone, step_event, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<intptr_t>(step)));
    }
    // The title ends a gap before them.
    if (row_x - g.disc / 5 < title_end) title_end = row_x - g.disc / 5;
  }

  if (pill && pill_left - g.disc / 5 < title_end) title_end = pill_left - g.disc / 5;

  // Home or the folder's name, one line between the circle and the time
  // (or the dots).
  const char* title_text = home ? home_title(tr.home) : (folder && folder->name[0] ? folder->name : tr.home);
  const int title_w = title_end > g.title_x ? title_end - g.title_x : 1;
  const lv_font_t* font = title_font(title_text, title_w);
  lv_obj_t* title = lv_label_create(bar);
  lv_obj_set_style_text_font(title, font, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(ui_theme::text()), 0);
  lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
  lv_obj_set_size(title, title_w, lv_font_get_line_height(font));
  lv_label_set_text(title, title_text);
  lv_obj_set_pos(title, g.title_x, g.center_y - lv_font_get_line_height(font) / 2);
}

void cancel_press() {
  if (g_press_dots) release_press(g_press_dots);
}

void rebuild(lv_obj_t* grid, uint16_t folder_id) {
  if (!grid) return;
  for (uint32_t i = lv_obj_get_child_count(grid); i-- > 0;) {
    lv_obj_t* child = lv_obj_get_child(grid, static_cast<int32_t>(i));
    if (is_bar(child)) lv_obj_delete(child);
  }
  build(grid, folder_id);
}

int dots_width(int count) {
  const DotSizes d = dot_sizes();
  return count * d.dot + (d.active - d.dot) + (count - 1) * d.gap;
}

lv_obj_t* create_dots(lv_obj_t* parent, int count, int active, int x, int y) {
  const DotSizes d = dot_sizes();
  lv_obj_t* dots = plain(parent);
  lv_obj_set_size(dots, dots_width(count), d.dot);
  lv_obj_set_pos(dots, x, y);
  // The grey dots, then the line (lay_out_line places them).
  for (int i = 0; i <= count; ++i) {
    lv_obj_t* dot = plain(dots);
    lv_obj_set_size(dot, i == count ? d.active : d.dot, d.dot);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(dot_color(i == count)), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
  }
  lay_out_line(dots, active, active, 0, 0);
  lv_obj_add_event_cb(dots, forget_dots, LV_EVENT_DELETE, nullptr);
  return dots;
}

void shown(lv_obj_t* grid, uint16_t folder_id, uint16_t previous_folder_id) {
  (void)previous_folder_id;
  const int page = tile_layouts::page_number(folder_id);
  lv_obj_t* dots = find_dots(grid);
  if (!dots || page < 0) return;
  // At rest on its own dot whatever came before: a page turn's line went
  // over on the old page (lead), which kept it there when it was left (user
  // 2026-10-13: Home -> page 1 -> a folder in it -> the Home swipe showed
  // Home's line on the second dot).
  if (g_motion.dots == dots) stop_motion();
  lay_out_line(dots, page, page, 0, 0);
}

void glide(lv_obj_t* dots, int from, int to, uint32_t duration) {
  if (!dots) return;
  if (from == to || !duration) {
    if (g_motion.dots == dots) stop_motion();
    lay_out_line(dots, to, to, 0, 0);
    return;
  }
  move_line(dots, from, to, 0.0f, 1.0f, 0, duration);
}

bool is_dots(const lv_obj_t* obj) { return obj && lv_obj_get_user_data(const_cast<lv_obj_t*>(obj)) == &g_dots_marker; }

// The built-in camera's circle while it streams (camera_indicator.h): part
// of the head like the time, so a popup lies over it like over the rest of
// the head. The page circle's size on the head's centre line, in the middle
// of the screen; with pages the dots hold the middle and it sits before the
// time (user 2026-10-12). The page circle's grey with the webcam icon in the
// camera's red (user 2026-10-11: "Grauer Kreis"). A tap ends the stream.
static void camera_circle_event(lv_event_t*) { local_camera::endStream(); }
static void add_camera_circle(lv_obj_t* bar, const Geometry& g, int right) {
  if (local_camera::indicatorStyle() != local_camera::IndicatorStyle::Pill ||
      !local_camera::indicatorActive())
    return;
  uint16_t pages[tile_layouts::kMaxPages];
  const int x = tile_layouts::pages(pages) ? right - g.disc : (grid_layout::screen_w() - g.disc) / 2;
  lv_obj_t* circle = lv_button_create(bar);
  lv_obj_remove_style_all(circle);
  lv_obj_remove_flag(circle, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_pos(circle, x, g.center_y - g.disc / 2);
  lv_obj_set_size(circle, g.disc, g.disc);
  lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(circle, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(circle, lv_color_hex(page_circle_tone().disc), 0);
  lv_obj_t* icon = lv_label_create(circle);
  lv_label_set_text(icon, getMdiChar("webcam").c_str());
  if (FONT_MDI_ICONS) lv_obj_set_style_text_font(icon, FONT_MDI_ICONS, 0);
  lv_obj_set_style_text_color(icon, lv_color_hex(settings_style::tone(head_base(), kCameraRed).icon), 0);
  lv_obj_center(icon);
  lv_obj_add_event_cb(circle, camera_circle_event, LV_EVENT_CLICKED, nullptr);
}

}  // namespace home_bar
