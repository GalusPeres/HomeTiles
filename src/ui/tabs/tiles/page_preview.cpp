#include "src/ui/tabs/tiles/page_preview.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cmath>

#include "src/core/config/config_manager.h"
#include "src/core/i18n/i18n.h"
#include "src/fonts/ui_fonts.h"
#include "src/tiles/config/grid_layout.h"
#include "src/tiles/config/tile_config.h"
#include "src/tiles/runtime/tile_renderer_fonts.h"
#include "src/tiles/config/tile_layouts.h"
#include "src/ui/popups/popup_layout.h"
#include "src/ui/shared/ui_surface_style.h"
#include "src/ui/shared/tone_color.h"
#include "src/ui/shared/ui_theme.h"
#include "src/ui/tabs/tiles/home_bar.h"
#include "src/ui/tabs/tiles/tab_tiles_unified.h"
#include "src/ui/ui_manager.h"

namespace page_preview {
namespace {

constexpr int kMaxPictures = static_cast<int>(tile_layouts::kMaxPages) + 1;
constexpr int kColumns = 3;
// Untouched this long it closes by itself (user 2026-10-13).
constexpr uint32_t kIdleCloseMs = 10000;

// The overview's fixed rules (user 2026-10-12: one set of rules for every
// panel, from the firmware's own values; mockup
// build/design-mockups/pages/dots-swipe.html):
//  1. Its frame is every popup's: corner kCardRadius + grid gap, the popup
//     hairline and shadow, the popup card color.
//  2. A picture is 22 % of the short screen side wide, three in a row.
//  3. In a picture the page's head (circle, title, time) and its tiles on a
//     small grid of their own (scaled down, gaps and corners shrank to a
//     pixel and it looked like a mosaic): gap and margin twice the scaled
//     grid gap, at least 3 px; the tiles' corner by the panel's rule (cell
//     height minus gap, a quarter: half tiles round, tile_radius.h).
//  4. Concentric outwards: picture corner = tile corner + margin; the card's
//     inner margin = card corner - picture corner, the same between the
//     pictures. Each page sits in a cell around picture and name (twice the
//     picture gap, at most a third of that margin; corner = picture corner
//     + its margin).
//  5. The card's top on the head circle's top line, its dots in a band as
//     high as the circle (user 2026-10-12: circle, pill and window on one
//     line).
//  6. The shown page's cell filled like a pressed one, a pressed page's the
//     same (user 2026-10-12: a line around it was too much); every name in
//     the text color, in the tile title font.
struct Geometry {
  int pic_w;
  int pic_h;
  int gap;        // between and around the small tiles
  float cell_w;   // a small cell
  float cell_h;
  int tile_radius;
  int pic_radius;
  int disc;       // the small head's circle
  int bar;        // its title and time lines' height
  int card_radius;
  int pad;        // between the pictures (card corner - picture corner)
  int cell_pad;   // a cell's margin around picture and name
  int cell_radius;
  int text_gap;
  const lv_font_t* font;
};

Geometry geometry() {
  const int w = grid_layout::screen_w();
  const int h = grid_layout::screen_h();
  const int side = w < h ? w : h;
  const grid_layout::Shown& shown = grid_layout::shown();
  Geometry g{};
  g.pic_w = static_cast<int>(std::lround(side * 0.22f));
  g.gap = std::max(3, static_cast<int>(std::lround(2.0f * GRID_GAP * g.pic_w / w)));
  g.cell_w = static_cast<float>(g.pic_w - 2 * g.gap - (shown.cols - 1) * g.gap) / shown.cols;
  g.cell_h = g.cell_w * GRID_CELL_H / GRID_CELL_W;
  g.tile_radius = std::max(2, static_cast<int>(std::lround((g.cell_h - g.gap) / 4.0f)));
  g.pic_radius = g.tile_radius + g.gap;
  g.disc = 2 * g.gap;
  g.bar = std::max(2, g.disc / 2);
  g.pic_h = static_cast<int>(std::lround(g.gap + g.disc + g.gap + grid_layout::shown_rows() * (g.cell_h + g.gap) - g.gap + g.gap));
  g.card_radius = popup_layout::kCardRadius + Device::kGridGap;
  g.pad = std::max(g.gap, g.card_radius - g.pic_radius);
  g.cell_pad = std::min(2 * g.gap, g.pad / 3);
  g.cell_radius = g.pic_radius + g.cell_pad;
  g.text_gap = 2 * g.gap;
  g.font = tile_layout::header_title_font();
  return g;
}

struct State {
  // The whole screen under the card: a tap there closes it. It draws
  // nothing, so it comes and goes without marking the screen for a redraw
  // (b601: the whole Home was repainted under the card, 145 ms a frame).
  lv_obj_t* catcher = nullptr;
  // The card on it: only its area (shadow included) is redrawn.
  lv_obj_t* card = nullptr;
  // The head's dot row, hidden while the card shows its own.
  lv_obj_t* dots = nullptr;
  // Every picture's tiles, picture i from first[i] to first[i + 1].
  PageTileRect* rects = nullptr;
  uint16_t first[kMaxPictures + 1] = {};
  Geometry geo{};
  // The pages' cells (picture and name), for a finger that slides over them.
  lv_obj_t* cells[kMaxPictures] = {};
  int count = 0;
  // Its own dot row and the page it marks (a swipe turns the page under it).
  lv_obj_t* card_dots = nullptr;
  int current = -1;
  // Closes it once the screen has not been touched for kIdleCloseMs.
  lv_timer_t* idle = nullptr;
};
State g_state;

void close_when_idle(lv_timer_t*) {
  if (lv_display_get_inactive_time(nullptr) >= kIdleCloseMs) close();
}

void forget_dots(lv_event_t* event) {
  if (lv_event_get_target(event) == g_state.dots) g_state.dots = nullptr;
}

// A picture: the screen in its color, the page's head as a circle and two
// lines, its tiles in their card colors on the small grid.
void picture_draw(lv_event_t* event) {
  lv_obj_t* picture = static_cast<lv_obj_t*>(lv_event_get_current_target(event));
  const int index = static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(picture)));
  if (!g_state.rects || index < 0 || index >= kMaxPictures) return;
  const Geometry& g = g_state.geo;
  lv_layer_t* layer = lv_event_get_layer(event);
  lv_area_t area;
  lv_obj_get_coords(picture, &area);
  auto box = [&](int x, int y, int w, int h, uint32_t color, int radius, bool line) {
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.radius = radius;
    dsc.bg_opa = LV_OPA_COVER;
    dsc.bg_color = lv_color_hex(color);
    if (line) {
      dsc.border_width = 1;
      dsc.border_color = ui_surface_style::frame_line_for(color);
      dsc.border_opa = LV_OPA_COVER;
    }
    lv_area_t a{area.x1 + x, area.y1 + y, area.x1 + x + w - 1, area.y1 + y + h - 1};
    lv_draw_rect(layer, &dsc, &a);
  };
  // The head: circle, title and time in the inactive dots' grey.
  const uint32_t head = home_bar::dot_color(false);
  const int mid = g.gap + g.disc / 2;
  box(g.gap, g.gap, g.disc, g.disc, head, LV_RADIUS_CIRCLE, false);
  box(g.gap + g.disc + g.gap, mid - g.bar / 2, 3 * g.disc, g.bar, head, LV_RADIUS_CIRCLE, false);
  const int time_w = (7 * g.disc + 2) / 4;
  box(g.pic_w - g.gap - time_w, mid - g.bar / 2, time_w, g.bar, head, LV_RADIUS_CIRCLE, false);
  // The tiles, a white card of the light theme with its frame line.
  const float top = static_cast<float>(g.gap + g.disc + g.gap);
  for (int i = g_state.first[index]; i < g_state.first[index + 1]; ++i) {
    const PageTileRect& rect = g_state.rects[i];
    const int x = static_cast<int>(std::lround(g.gap + rect.col * (g.cell_w + g.gap)));
    const int y = static_cast<int>(std::lround(top + rect.row * (g.cell_h + g.gap)));
    const int w = static_cast<int>(std::lround(rect.span_w * (g.cell_w + g.gap) - g.gap));
    const int h = static_cast<int>(std::lround(rect.span_h * (g.cell_h + g.gap) - g.gap));
    const bool line = ui_theme::theme_light() && rect.color == ui_theme::kLight.card;
    box(x, y, w, h, rect.color, g.tile_radius, line);
  }
}

// A tap on a page's cell shows it (a finger that moved taps nothing,
// ui_manager.cpp).
void on_picture(lv_event_t* event) {
  const int index = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
  close();
  uiManager.showPage(index);
}

// Any lift beside the pictures closes it (user 2026-10-13: a tap that moved
// a little did not close it at once).
void on_close(lv_event_t*) { close(); }

// Beside its dots a tap turns one page to that side and it stays open, its
// line gliding along (user 2026-10-14, like the head's).
void on_step(lv_event_t* event) {
  const int to = g_state.current + static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
  if (to >= 0 && to < g_state.count) uiManager.showPage(to);
}

// The page whose cell lies under a point, or -1.
int cell_at(const lv_point_t& point) {
  for (int i = 0; i < g_state.count; ++i) {
    lv_area_t area;
    if (!g_state.cells[i]) continue;
    lv_obj_get_coords(g_state.cells[i], &area);
    if (point.x >= area.x1 && point.x <= area.x2 && point.y >= area.y1 && point.y <= area.y2) return i;
  }
  return -1;
}

lv_obj_t* plain(lv_obj_t* parent) {
  lv_obj_t* obj = lv_obj_create(parent);
  lv_obj_remove_style_all(obj);
  lv_obj_remove_flag(obj, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  return obj;
}

// A part a touch reaches: `code` is its tap (CLICKED) or any lift (RELEASED).
void tappable(lv_obj_t* obj, lv_event_cb_t on_tap, void* data, lv_event_code_t code) {
  lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(obj, on_tap, code, data);
}

}  // namespace

bool is_open() { return g_state.catcher != nullptr; }

bool contains(lv_obj_t* obj) {
  for (; obj && g_state.catcher; obj = lv_obj_get_parent(obj)) {
    if (obj == g_state.catcher) return true;
  }
  return false;
}

void point_at(const lv_point_t& point) {
  const int under = cell_at(point);
  for (int i = 0; i < g_state.count; ++i) {
    if (!g_state.cells[i]) continue;
    if (i == under) lv_obj_add_state(g_state.cells[i], LV_STATE_PRESSED);
    else lv_obj_remove_state(g_state.cells[i], LV_STATE_PRESSED);
  }
}

void lift_at(const lv_point_t& point) {
  const int under = cell_at(point);
  point_at({-1, -1});
  if (under < 0) return;
  close();
  uiManager.showPage(under);
}

// The shown page's cell filled like a pressed one, the others clear.
static void mark(int page) {
  g_state.current = page;
  for (int i = 0; i < g_state.count; ++i) {
    if (g_state.cells[i]) lv_obj_set_style_bg_opa(g_state.cells[i], i == page ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  }
}

bool lead(int from, int to, uint32_t duration) {
  if (!is_open() || !g_state.card_dots || to < 0 || to >= g_state.count) return false;
  home_bar::glide(g_state.card_dots, from, to, duration);
  mark(to);
  return true;
}

void follow(uint16_t folder_id) {
  if (!is_open()) return;
  const int page = tile_layouts::page_number(folder_id);
  if (page < 0 || page >= g_state.count) {
    close();
    return;
  }
  // At rest on the shown page, whatever moved before (user 2026-10-13: a
  // line taken over by another kept the old page).
  home_bar::glide(g_state.card_dots, page, page, 0);
  mark(page);
}

void close() {
  if (g_state.idle) lv_timer_delete(g_state.idle);
  g_state.idle = nullptr;
  if (g_state.catcher) {
    // The card's area is redrawn; the catcher goes without marking the rest.
    if (g_state.card) lv_obj_invalidate(g_state.card);
    lv_display_t* display = lv_obj_get_display(g_state.catcher);
    if (display) lv_display_enable_invalidation(display, false);
    lv_obj_delete(g_state.catcher);
    if (display) lv_display_enable_invalidation(display, true);
  }
  g_state.catcher = nullptr;
  g_state.card = nullptr;
  for (lv_obj_t*& cell : g_state.cells) cell = nullptr;
  g_state.count = 0;
  g_state.card_dots = nullptr;
  g_state.current = -1;
  if (g_state.rects) heap_caps_free(g_state.rects);
  g_state.rects = nullptr;
  if (g_state.dots) {
    lv_obj_remove_event_cb(g_state.dots, forget_dots);
    lv_obj_remove_flag(g_state.dots, LV_OBJ_FLAG_HIDDEN);
  }
  g_state.dots = nullptr;
}

void open(lv_obj_t* dots) {
  if (is_open()) {
    close();
    return;
  }
  uint16_t pages[tile_layouts::kMaxPages];
  const int count = static_cast<int>(tile_layouts::pages(pages)) + 1;
  const int current = tile_layouts::page_number(tileConfig.getActiveFolderId());
  if (count < 2 || current < 0) return;
  // The pictures' tiles: the built grids' card colors, else the stored ones.
  const size_t capacity = static_cast<size_t>(count) * TILES_PER_GRID;
  g_state.rects = static_cast<PageTileRect*>(
      heap_caps_malloc(capacity * sizeof(PageTileRect), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!g_state.rects) g_state.rects = static_cast<PageTileRect*>(heap_caps_malloc(capacity * sizeof(PageTileRect), MALLOC_CAP_8BIT));
  if (!g_state.rects) {
    Serial.println("[Pages] No memory for the preview");
    return;
  }
  const uint32_t started = millis();
  size_t used = 0;
  for (int i = 0; i < count; ++i) {
    g_state.first[i] = static_cast<uint16_t>(used);
    const uint16_t folder = i == 0 ? TileConfig::rootFolderId() : pages[i - 1];
    used += tiles_page_schematic(folder, g_state.rects + used, TILES_PER_GRID);
  }
  g_state.first[count] = static_cast<uint16_t>(used);

  g_state.geo = geometry();
  const Geometry& geo = g_state.geo;
  const home_bar::Geometry head = home_bar::geometry();
  const home_bar::DotSizes dot = home_bar::dot_sizes();
  const int cols = count < kColumns ? count : kColumns;
  const int rows = (count + cols - 1) / cols;
  const int line = lv_font_get_line_height(geo.font);
  const int cell_w = geo.pic_w + 2 * geo.cell_pad;
  const int cell_h = geo.cell_pad + geo.pic_h + geo.text_gap + line + geo.cell_pad;
  const int between = geo.pad - 2 * geo.cell_pad;
  const int margin = geo.pad - geo.cell_pad;
  const int card_w = cols * cell_w + (cols - 1) * between + 2 * margin;
  const int card_h = head.disc + rows * cell_h + (rows - 1) * between + margin;
  // Grown out of the dots: its top on the head circle's top line, its dot
  // row exactly on the head's.
  const int card_x = (grid_layout::screen_w() - card_w) / 2;
  const int card_y = head.center_y - head.disc / 2;

  // Built without marking anything; the card marks its own area at the end.
  lv_display_t* display = lv_obj_get_display(lv_layer_top());
  if (display) lv_display_enable_invalidation(display, false);
  g_state.catcher = plain(lv_layer_top());
  lv_obj_set_size(g_state.catcher, grid_layout::screen_w(), grid_layout::screen_h());
  tappable(g_state.catcher, on_close, nullptr, LV_EVENT_RELEASED);

  // The popups' frame: their card color, corner, hairline and shadow. A tap
  // on it beside the pictures closes it too (user 2026-10-12).
  lv_obj_t* card = plain(g_state.catcher);
  tappable(card, on_close, nullptr, LV_EVENT_RELEASED);
  lv_obj_set_pos(card, card_x, card_y);
  lv_obj_set_size(card, card_w, card_h);
  lv_obj_set_style_bg_color(card, lv_color_hex(ui_theme::popup_card()), 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(card, geo.card_radius, 0);
  ui_surface_style::apply_popup_border(card, lv_color_hex(ui_theme::text()),
                                       static_cast<lv_opa_t>(popup_layout::kPopupBorderOpa));
  popup_layout::apply_card_shadow(card);

  const int dots_w = home_bar::dots_width(count);
  lv_obj_t* card_dots = home_bar::create_dots(card, count, current, (grid_layout::screen_w() - dots_w) / 2 - card_x,
                                              head.center_y - dot.dot / 2 - card_y);
  // A tap on them closes it again, like on the head's (user 2026-10-12).
  tappable(card_dots, on_close, nullptr, LV_EVENT_RELEASED);
  lv_obj_set_ext_click_area(card_dots, geo.pad);
  // Beside them as wide as the head's (home_bar::step_zone_width) within
  // the card, the head row's height: a tap turns one page.
  const int dots_x = (grid_layout::screen_w() - dots_w) / 2 - card_x;
  for (const int step : {-1, 1}) {
    const int x = step < 0 ? LV_MAX(0, dots_x - home_bar::step_zone_width()) : dots_x + dots_w;
    const int w = step < 0 ? dots_x - x : LV_MIN(home_bar::step_zone_width(), card_w - x);
    if (w <= 0) continue;
    lv_obj_t* zone = plain(card);
    lv_obj_set_size(zone, w, head.disc);
    lv_obj_set_pos(zone, x, 0);
    tappable(zone, on_step, reinterpret_cast<void*>(static_cast<intptr_t>(step)), LV_EVENT_CLICKED);
  }

  const i18n::Strings& tr = i18n::strings(configManager.getConfig().language);
  const lv_color_t pressed = lv_color_hex(tone_color::pressed_card(ui_theme::popup_card()));
  for (int i = 0; i < count; ++i) {
    // The page's cell: picture and name; a tap anywhere on it goes there.
    lv_obj_t* cell = plain(card);
    lv_obj_set_pos(cell, margin + (i % cols) * (cell_w + between), head.disc + (i / cols) * (cell_h + between));
    lv_obj_set_size(cell, cell_w, cell_h);
    lv_obj_set_style_radius(cell, geo.cell_radius, 0);
    lv_obj_set_style_bg_color(cell, pressed, 0);
    lv_obj_set_style_bg_opa(cell, i == current ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, LV_STATE_PRESSED);
    tappable(cell, on_picture, reinterpret_cast<void*>(static_cast<intptr_t>(i)), LV_EVENT_CLICKED);
    g_state.cells[i] = cell;

    lv_obj_t* picture = plain(cell);
    lv_obj_set_user_data(picture, reinterpret_cast<void*>(static_cast<intptr_t>(i)));
    lv_obj_set_pos(picture, geo.cell_pad, geo.cell_pad);
    lv_obj_set_size(picture, geo.pic_w, geo.pic_h);
    lv_obj_set_style_radius(picture, geo.pic_radius, 0);
    lv_obj_set_style_bg_color(picture, lv_color_hex(ui_theme::screen()), 0);
    lv_obj_set_style_bg_opa(picture, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(picture, picture_draw, LV_EVENT_DRAW_MAIN_END, nullptr);

    const FolderEntry* folder = i == 0 ? nullptr : tileConfig.getFolder(pages[i - 1]);
    lv_obj_t* name = lv_label_create(cell);
    lv_obj_set_style_text_font(name, geo.font, 0);
    lv_obj_set_style_text_color(name, lv_color_hex(ui_theme::text()), 0);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_size(name, geo.pic_w, line);
    lv_obj_set_pos(name, geo.cell_pad, geo.cell_pad + geo.pic_h + geo.text_gap);
    lv_label_set_text(name, i == 0 ? home_bar::home_title(tr.home) : !folder || !folder->name[0] ? tr.home : folder->name);
  }
  // LVGL places new objects on its next layout pass, which would mark the
  // catcher's whole area after all: placed now, while nothing is marked.
  lv_obj_update_layout(g_state.catcher);
  if (display) lv_display_enable_invalidation(display, true);
  lv_obj_invalidate(card);

  g_state.card = card;
  g_state.count = count;
  g_state.card_dots = card_dots;
  g_state.current = current;
  g_state.idle = lv_timer_create(close_when_idle, 500, nullptr);
  // The head's own dots hide under the card's.
  g_state.dots = dots;
  if (dots) {
    lv_obj_add_flag(dots, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(dots, forget_dots, LV_EVENT_DELETE, nullptr);
  }
  Serial.printf("[Pages] Preview of %d pages (%u tiles) %dx%d in %lu ms\n", count, static_cast<unsigned>(used),
                card_w, card_h, static_cast<unsigned long>(millis() - started));
}

}  // namespace page_preview
