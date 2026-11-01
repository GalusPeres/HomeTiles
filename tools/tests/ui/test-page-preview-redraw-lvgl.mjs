// The pages' overview (tap on the head dots) opened slowly: b601 logged
// 145 ms for its first frame with 1024000 pixels, the whole screen, because
// open() put a transparent screen-wide tap catcher on the top layer and
// creating it marked the whole screen, so LVGL repainted every Home tile
// under the card. The catcher draws nothing; it now comes and goes without
// marking anything and only the card's area (shadow included) is redrawn.
// The real page_preview.cpp runs on real LVGL (its neighbours are small
// stand-ins): opening and closing redraw only the card's area, the pixels
// equal a full redraw, the catcher still covers the screen for taps.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {lvglHost} from '../../lib/lvgl-host.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n?/g, '\n');
const preview = read('src/ui/tabs/tiles/page_preview.cpp');
const fn = name => {
  const found = cppFunctionDefinitions(preview).find(item => item.name === name);
  assert.ok(found, name);
  return found.source;
};

// Source: the catcher is made, placed and deleted with invalidation off (LVGL
// places objects on its next layout pass, which would mark the catcher's
// area after all), the card marks its own area, the switch goes back on.
const open = fn('open');
assert.ok(open.indexOf('lv_display_enable_invalidation(display, false);') < open.indexOf('g_state.catcher = plain(lv_layer_top());'));
const placed = open.indexOf('lv_obj_update_layout(g_state.catcher);\n  if (display) lv_display_enable_invalidation(display, true);\n  lv_obj_invalidate(card);');
assert.ok(placed > open.lastIndexOf('lv_label_set_text(name,'), 'placed while nothing is marked, then the card marks its area');
const close = fn('close');
assert.match(close, /if \(g_state\.card\) lv_obj_invalidate\(g_state\.card\);\s*lv_display_t\* display = lv_obj_get_display\(g_state\.catcher\);\s*if \(display\) lv_display_enable_invalidation\(display, false\);\s*lv_obj_delete\(g_state\.catcher\);\s*if \(display\) lv_display_enable_invalidation\(display, true\);/);

const host = await lvglHost(root);
if (!host) {
  console.log('SKIP: the overview redraw run needs LVGL and a host compiler (source checks passed)');
  process.exit(0);
}

const out = path.join(root, 'build/tests/page-preview-redraw');
const stub = path.join(out, 'stub');
const stubs = {
  'Arduino.h': `#pragma once
#include <cstdio>
#include <cstdint>
struct HostSerial {
  template <typename... A> void printf(const char* f, A... a) { std::printf(f, a...); }
  void println(const char* s) { std::puts(s); }
};
inline HostSerial Serial;
inline uint32_t millis() { return 0; }`,
  'esp_heap_caps.h': `#pragma once
#include <cstdlib>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
inline void* heap_caps_malloc(size_t size, int) { return std::malloc(size); }
inline void heap_caps_free(void* p) { std::free(p); }`,
  'src/core/config/config_manager.h': `#pragma once
#include <cstdint>
struct HostConfig { uint8_t language = 0; };
struct HostConfigManager { HostConfig config; const HostConfig& getConfig() const { return config; } };
inline HostConfigManager configManager;`,
  'src/core/i18n/i18n.h': `#pragma once
#include <cstdint>
namespace i18n {
struct Strings { const char* home; };
inline const Strings& strings(uint8_t) { static const Strings s{"Home"}; return s; }
}`,
  'src/fonts/ui_fonts.h': '#pragma once',
  'src/tiles/config/grid_layout.h': `#pragma once
// The Guition V2 grid (1280x800, 6 x 4 cells of 194 x 152, gap 16).
#define GRID_GAP 16
#define GRID_CELL_W 194
#define GRID_CELL_H 152
namespace grid_layout {
struct Shown { int cols; };
inline int screen_w() { return 1280; }
inline int screen_h() { return 800; }
inline const Shown& shown() { static const Shown s{6}; return s; }
inline int shown_rows() { return 4; }
}`,
  'src/tiles/config/tile_config.h': `#pragma once
#include <cstdint>
#define TILES_PER_GRID 64
struct FolderEntry { char name[32]; };
struct TileConfig {
  FolderEntry folders[8] = {{"Home"}, {"Licht"}, {"Klima"}, {"Energie"}, {"Medien"}, {"Kameras"}, {""}, {""}};
  uint16_t getActiveFolderId() const { return 0; }
  const FolderEntry* getFolder(uint16_t id) const { return id < 8 ? &folders[id] : nullptr; }
  static uint16_t rootFolderId() { return 0; }
};
inline TileConfig tileConfig;`,
  'src/tiles/runtime/tile_renderer_fonts.h': `#pragma once
#include <lvgl.h>
namespace tile_layout { inline const lv_font_t* header_title_font() { return LV_FONT_DEFAULT; } }`,
  'src/tiles/config/tile_layouts.h': `#pragma once
#include <cstddef>
#include <cstdint>
namespace tile_layouts {
constexpr size_t kMaxPages = 8;
inline size_t pages(uint16_t* out) { for (uint16_t i = 0; i < 5; ++i) out[i] = i + 1; return 5; }
inline int page_number(uint16_t folder) { return folder <= 5 ? folder : -1; }
}`,
  'src/ui/popups/popup_layout.h': `#pragma once
#include <lvgl.h>
struct Device { static constexpr int kGridGap = 16; };
namespace popup_layout {
constexpr int kCardRadius = 22;
constexpr int kPopupBorderOpa = 51;
// As popup_layout.h: the cached shadow, at most 28 px wide.
inline void apply_card_shadow(lv_obj_t* obj) {
  lv_obj_set_style_shadow_width(obj, 28, 0);
  lv_obj_set_style_shadow_color(obj, lv_color_black(), 0);
  lv_obj_set_style_shadow_opa(obj, LV_OPA_40, 0);
  lv_obj_set_style_shadow_spread(obj, 2, 0);
}
}`,
  'src/ui/shared/ui_surface_style.h': `#pragma once
#include <lvgl.h>
#include <cstdint>
namespace ui_surface_style {
inline void apply_popup_border(lv_obj_t* obj, lv_color_t color, lv_opa_t opa) {
  lv_obj_set_style_outline_width(obj, 1, 0);
  lv_obj_set_style_outline_color(obj, color, 0);
  lv_obj_set_style_outline_opa(obj, opa, 0);
}
inline lv_color_t frame_line_for(uint32_t) { return lv_color_hex(0xD0D0D0); }
}`,
  'src/ui/shared/tone_color.h': `#pragma once
#include <cstdint>
namespace tone_color { inline uint32_t pressed_card(uint32_t c) { return c + 0x141414; } }`,
  'src/ui/shared/ui_theme.h': `#pragma once
#include <cstdint>
namespace ui_theme {
struct Palette { uint32_t card; };
inline constexpr Palette kLight{0xFFFFFF};
inline uint32_t popup_card() { return 0x1A1A1A; }
inline uint32_t text() { return 0xF2F2F2; }
inline uint32_t screen() { return 0x000000; }
inline bool theme_light() { return false; }
}`,
  'src/ui/tabs/tiles/home_bar.h': `#pragma once
#include <lvgl.h>
#include <cstdint>
namespace home_bar {
struct Geometry { int center_y; int disc; };
inline Geometry geometry() { return {64, 72}; }
struct DotSizes { int dot; };
inline DotSizes dot_sizes() { return {12}; }
inline int dots_width(int count) { return count * 24; }
inline lv_obj_t* create_dots(lv_obj_t* parent, int count, int, int x, int y) {
  lv_obj_t* dots = lv_obj_create(parent);
  lv_obj_remove_style_all(dots);
  lv_obj_set_pos(dots, x, y);
  lv_obj_set_size(dots, dots_width(count), 12);
  lv_obj_set_style_bg_color(dots, lv_color_hex(0x777777), 0);
  lv_obj_set_style_bg_opa(dots, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(dots, 6, 0);
  return dots;
}
inline int step_zone_width() { return 144; }
inline uint32_t dot_color(bool) { return 0x5A5A5A; }
inline void glide(lv_obj_t*, int, int, uint32_t) {}
inline const char* home_title(const char* title) { return title; }
}`,
  'src/ui/tabs/tiles/tab_tiles_unified.h': `#pragma once
#include <cstddef>
#include <cstdint>
struct PageTileRect { float col; float row; float span_w; float span_h; uint32_t color; };
inline size_t tiles_page_schematic(uint16_t folder, PageTileRect* out, size_t max) {
  size_t n = 0;
  for (int r = 0; r < 4 && n < max; ++r)
    for (int c = 0; c < 6 && n < max; c += 2) out[n++] = {float(c), float(r), 2.0f, 1.0f, 0x1A1A1Au + folder * 0x0A0A0Au};
  return n;
}`,
  'src/ui/ui_manager.h': `#pragma once
struct HostUIManager { int shown = -1; void showPage(int index) { shown = index; } };
inline HostUIManager uiManager;`,
};
for (const [file, text] of Object.entries(stubs)) {
  fs.mkdirSync(path.dirname(path.join(stub, file)), {recursive: true});
  fs.writeFileSync(path.join(stub, file), text + '\n');
}

const cpp = String.raw`
#include <lvgl.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include "src/ui/tabs/tiles/page_preview.h"

static constexpr int W = 1280, H = 800, kBand = 28;
static std::vector<uint16_t> g_screen(W * H);
static std::vector<uint16_t> g_band(W * kBand);
static long g_pixels = 0;

static void flush(lv_display_t* d, const lv_area_t* a, uint8_t* px) {
  const int w = a->x2 - a->x1 + 1;
  for (int y = a->y1; y <= a->y2; ++y)
    memcpy(&g_screen[y * W + a->x1], reinterpret_cast<uint16_t*>(px) + (y - a->y1) * w, w * 2);
  g_pixels += static_cast<long>(w) * (a->y2 - a->y1 + 1);
  lv_display_flush_ready(d);
}

static long refresh() {
  g_pixels = 0;
  lv_refr_now(nullptr);
  return g_pixels;
}

static unsigned diff(const std::vector<uint16_t>& a, const std::vector<uint16_t>& b) {
  unsigned n = 0;
  for (size_t i = 0; i < a.size(); ++i) n += a[i] != b[i];
  return n;
}

int main() {
  lv_init();
  lv_display_t* display = lv_display_create(W, H);
  lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(display, g_band.data(), nullptr, g_band.size() * 2, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(display, flush);

  // A Home screen: head and 6 x 4 tiles.
  lv_obj_t* screen = lv_obj_create(nullptr);
  lv_obj_remove_style_all(screen);
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  for (int i = 0; i < 24; ++i) {
    lv_obj_t* tile = lv_obj_create(screen);
    lv_obj_remove_style_all(tile);
    lv_obj_set_pos(tile, 18 + (i % 6) * 210, 128 + (i / 6) * 168);
    lv_obj_set_size(tile, 194, 152);
    lv_obj_set_style_radius(tile, 31, 0);
    lv_obj_set_style_bg_color(tile, lv_color_hex(0x1A1A1A + i * 0x040404), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_t* label = lv_label_create(tile);
    lv_label_set_text_fmt(label, "Tile %d", i);
    lv_obj_set_style_text_color(label, lv_color_hex(0xF2F2F2), 0);
    lv_obj_set_pos(label, 16, 100);
  }
  lv_screen_load(screen);
  const long first = refresh();
  const std::vector<uint16_t> home = g_screen;

  page_preview::open(nullptr);
  lv_obj_t* catcher = lv_obj_get_child(lv_layer_top(), 0);
  lv_obj_t* card = catcher ? lv_obj_get_child(catcher, 0) : nullptr;
  if (!card) { std::printf("{\"error\":\"no card\"}\n"); return 1; }
  lv_area_t c;
  lv_obj_get_coords(card, &c);
  int32_t ext = 0;
  lv_obj_send_event(card, LV_EVENT_REFR_EXT_DRAW_SIZE, &ext);
  const long bound = static_cast<long>(lv_area_get_width(&c) + 2 * ext) * (lv_area_get_height(&c) + 2 * ext);
  const long card_px = static_cast<long>(lv_area_get_width(&c)) * lv_area_get_height(&c);
  const long opened = refresh();
  const std::vector<uint16_t> shown = g_screen;
  lv_obj_invalidate(lv_screen_active());
  refresh();
  const unsigned open_diffs = diff(shown, g_screen);
  const int catcher_w = lv_obj_get_width(catcher), catcher_h = lv_obj_get_height(catcher);
  const bool enabled_open = lv_display_is_invalidation_enabled(display);

  page_preview::close();
  const long closed = refresh();
  const unsigned close_diffs = diff(home, g_screen);
  const bool enabled_closed = lv_display_is_invalidation_enabled(display);

  std::printf("{\"first\":%ld,\"cardW\":%d,\"cardH\":%d,\"ext\":%d,\"cardPx\":%ld,\"bound\":%ld,\"opened\":%ld,"
              "\"openDiffs\":%u,\"closed\":%ld,\"closeDiffs\":%u,\"catcherW\":%d,\"catcherH\":%d,"
              "\"enabledOpen\":%d,\"enabledClosed\":%d,\"open\":%d}\n",
              first, lv_area_get_width(&c), lv_area_get_height(&c), ext, card_px, bound, opened, open_diffs, closed,
              close_diffs, catcher_w, catcher_h, enabled_open ? 1 : 0, enabled_closed ? 1 : 0,
              page_preview::is_open() ? 1 : 0);
  return 0;
}
`;

fs.mkdirSync(out, {recursive: true});
const source = path.join(out, 'test.cpp');
fs.writeFileSync(source, cpp);
const binary = path.join(out, process.platform === 'win32' ? 'test.exe' : 'test');
let result = spawnSync(host.cxx, ['-I', stub, ...host.flags, '-std=c++17', '-O1', '-w', source,
  path.join(root, 'src/ui/tabs/tiles/page_preview.cpp'), host.archive, '-o', binary], {encoding: 'utf8'});
assert.equal(result.status, 0, result.stdout + result.stderr);
result = spawnSync(binary, [], {encoding: 'utf8'});
assert.equal(result.status, 0, result.stdout + result.stderr);
const run = JSON.parse(result.stdout.trim().split('\n').at(-1));
const screen = 1280 * 800;
assert.equal(run.first, screen, 'the first frame draws the whole screen');
assert.equal(run.cardW, 648, `the V2 card as on the panel ${JSON.stringify(run)}`);
assert.ok(run.opened >= run.cardPx && run.opened <= run.bound,
  `opening redraws the card's area only, not the screen ${JSON.stringify(run)}`);
assert.ok(run.opened < screen * 0.4, `well under the whole screen ${JSON.stringify(run)}`);
assert.equal(run.openDiffs, 0, 'every pixel as with a full redraw');
assert.equal(run.catcherW, 1280);
assert.equal(run.catcherH, 800, 'the catcher still covers the screen for taps');
assert.ok(run.closed >= run.cardPx && run.closed <= run.bound, `closing redraws the card's area only ${JSON.stringify(run)}`);
assert.equal(run.closeDiffs, 0, 'after closing the screen is Home again');
assert.equal(run.enabledOpen, 1);
assert.equal(run.enabledClosed, 1, 'invalidation is switched back on');
assert.equal(run.open, 0);
console.log(`PASS: the pages overview redraws only its card ${JSON.stringify(run)}`);
