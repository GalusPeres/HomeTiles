// A screensaver tile with a low fill opacity (user 2026-10-10: Opacity 30 %)
// drew a black circle behind its icon: card_of() walked past a card below
// 50 % fill to the black overlay behind the picture. A tile card (a button)
// over a see-through parent now stops the walk. A Transparent Clock/Text
// card on a page lies on the opaque grid: its header circle keeps the grid's
// color (stopping at every button drew a 25 % black circle in the light
// theme). Runs the production card_of/disc_fill on real LVGL.
import {radiusPolicyHost, surfaceStyleHost} from '../../lib/surface-style-host.mjs';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {lvglHost} from '../../lib/lvgl-host.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = p => fs.readFileSync(path.join(root, p), 'utf8').replace(/\r\n/g, '\n');

const disc = read('src/tiles/runtime/tile_icon_disc.h');
assert.match(disc, /lv_obj_get_style_bg_opa\(host, LV_PART_MAIN\) < LV_OPA_50 &&\s*!over_picture\(host\); \+\+depth\) \{/);
// The circle of a see-through card is a veil in the card's own color.
assert.match(disc, /see_through_card = see_through\(host\);/);
// Tile renderers create their cards as buttons; the screensaver's slot grid
// is see-through, the page grid opaque.
for (const file of ['sensor/renderer.cpp', 'switch/renderer.cpp', 'binary_sensor/renderer.cpp', 'energy/renderer.cpp',
  'clock/renderer.cpp', 'text/renderer.cpp']) {
  assert.match(read(`src/types/${file}`), /= lv_button_create\(parent\);/, file);
}
assert.match(read('src/ui/screensaver/image_screensaver.cpp'), /lv_obj_set_style_bg_opa\(st->slot_grid, LV_OPA_TRANSP, 0\);/);
assert.match(read('src/ui/tabs/tiles/tab_tiles_unified.cpp'), /lv_obj_set_style_bg_opa\(grid, LV_OPA_COVER, 0\);/);

const host = await lvglHost(root);
if (!host) {
  console.log('SKIP: see-through disc card needs LVGL and a host compiler');
  process.exit(0);
}
const out = path.join(root, 'build/tests/see-through-disc-card');
fs.mkdirSync(out, {recursive: true});
const fonts = read('src/tiles/runtime/tile_renderer_fonts.h').replace(/^#include.*$/gm, '')
  .replace('#pragma once', '').replaceAll('constexpr lv_coord_t', 'constexpr long');
const cpp = String.raw`
#include <lvgl.h>
#include <atomic>
#include <cstdio>
#include <string>
#include "src/ui/shared/title_label.h"
extern "C" { LV_FONT_DECLARE(ui_font_12);LV_FONT_DECLARE(ui_font_14);LV_FONT_DECLARE(ui_font_16);LV_FONT_DECLARE(ui_font_20);LV_FONT_DECLARE(ui_font_24);LV_FONT_DECLARE(ui_font_28);LV_FONT_DECLARE(ui_font_32);LV_FONT_DECLARE(ui_font_40);LV_FONT_DECLARE(mdi_icons_32);LV_FONT_DECLARE(mdi_icons_48); }
${fonts}
#define FONT_MDI_ICONS (&mdi_icons_48)
${radiusPolicyHost(root)}
#include "src/core/config/icon_glow.h"
struct Config{bool tile_borders=true;bool icon_discs=true;uint8_t icon_glow=icon_glow::kDefault;int tile_radius=tile_radius::kMinimum;const char*language="en";};struct Manager{Config cfg;const Config&getConfig(){return cfg;}}configManager;
${surfaceStyleHost(root)}
constexpr int GRID_CELL_H=CELL_H,GRID_GAP=GAP;
#include "src/types/climate/layout.h"
#include "src/tiles/runtime/tile_press.h"
${read('src/tiles/runtime/tile_icon_disc.h').replace(/^#.*$/gm, '')}
static int failures = 0;
// One card with an icon circle on 'parent'; reports the circle's host.
static void check(const char* name, lv_obj_t* parent, uint32_t card_rgb, lv_opa_t card_opa, bool want_card,
                  uint32_t want_host_rgb, bool want_see_through) {
  lv_obj_t* card = lv_button_create(parent);
  lv_obj_set_style_bg_color(card, lv_color_hex(card_rgb), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(card, card_opa, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_t* icon = lv_label_create(card);
  lv_label_set_text(icon, "A");
  lv_obj_set_style_text_color(icon, lv_color_hex(ui_theme::icon()), 0);
  lv_obj_t* round = tile_icon_disc::add_round(card, icon);
  uint32_t host_rgb = 0;
  bool see_through = false;
  tile_icon_disc::disc_fill(round, false, host_rgb, see_through);
  const bool on_card = tile_icon_disc::card_of(round) == card;
  if (on_card != want_card || host_rgb != want_host_rgb || see_through != want_see_through) {
    std::printf("FAIL %s: host %s #%06X see-through %d, expected %s #%06X %d\n", name, on_card ? "card" : "behind",
                (unsigned)host_rgb, see_through, want_card ? "card" : "behind", (unsigned)want_host_rgb,
                want_see_through);
    ++failures;
  }
  lv_obj_delete(card);
}
int main() {
  lv_init();
  static uint32_t px[64 * 64];
  lv_display_t* display = lv_display_create(64, 64);
  lv_display_set_color_format(display, LV_COLOR_FORMAT_XRGB8888);
  lv_display_set_buffers(display, px, nullptr, sizeof(px), LV_DISPLAY_RENDER_MODE_FULL);
  for (int theme = 0; theme < 2; ++theme) {
    ui_theme::set(theme);
    // A page: the opaque grid in the screen's color.
    lv_obj_t* grid = lv_obj_create(lv_screen_active());
    lv_obj_set_style_bg_color(grid, lv_color_hex(ui_theme::screen()), 0);
    lv_obj_set_style_bg_opa(grid, LV_OPA_COVER, 0);
    check(theme ? "light Transparent Clock/Text" : "dark Transparent Clock/Text", grid, 0x000000, LV_OPA_TRANSP,
          false, ui_theme::screen(), false);
    check(theme ? "light page tile" : "dark page tile", grid, 0x2A6FDB, LV_OPA_COVER, true, 0x2A6FDB, false);
    // The screensaver: the black overlay, the picture, the see-through slot grid.
    lv_obj_t* overlay = lv_obj_create(lv_screen_active());
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
    lv_obj_t* slots = lv_obj_create(overlay);
    lv_obj_set_style_bg_opa(slots, LV_OPA_TRANSP, 0);
    check("screensaver tile at 30 %", slots, 0x2A6FDB, 77, true, 0x2A6FDB, true);
    check("screensaver tile without fill", slots, 0x2A6FDB, LV_OPA_TRANSP, true, 0x2A6FDB, true);
    lv_obj_delete(grid);
    lv_obj_delete(overlay);
  }
  if (failures) return 1;
  std::printf("OK\n");
  return 0;
}
`;
const src = path.join(out, 'test.cpp');
const binary = path.join(out, process.platform === 'win32' ? 'test.exe' : 'test');
fs.writeFileSync(src, cpp);
let result = spawnSync(host.cxx, [...host.flags, '-std=c++17', '-DSCREEN_WIDTH=1280', '-DSCREEN_HEIGHT=800',
  '-DCELL_W=168', '-DCELL_H=145', '-DGAP=16', src, host.archive, '-o', binary], {encoding: 'utf8'});
assert.equal(result.status, 0, result.stdout + result.stderr);
result = spawnSync(binary, [], {encoding: 'utf8'});
assert.equal(result.status, 0, result.stdout + result.stderr);
console.log('Circles on see-through screensaver cards take the card color; Transparent page tiles the grid\'s');
