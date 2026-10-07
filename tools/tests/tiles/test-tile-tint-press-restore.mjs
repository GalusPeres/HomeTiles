// A Light tile with tile color "From icon" got one press step lighter after
// every on/off tapped on the tile (24 -> 40 -> 72 grey on the Guition V2):
// the tap tints the card while it is still pressed, and set_tile_tint kept
// that pressed color as the card's own color for clear_tile_tint to restore.
// Runs the production set_tile_tint/clear_tile_tint on a real LVGL card.
import {radiusPolicyHost, surfaceStyleHost} from '../../lib/surface-style-host.mjs';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';
import {lvglHost} from '../../lib/lvgl-host.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = p => fs.readFileSync(path.join(root, p), 'utf8').replace(/\r\n/g, '\n');
const source = read('src/tiles/runtime/tile_icon_source.cpp');
const fn = name => {
  const found = cppFunctionDefinitions(source).find(f => f.name === name);
  assert(found, name);
  return found.source;
};

assert.match(fn('set_tile_tint'), /lv_color_hex\(tile_icon_disc::card_state_color\(card, false\)\), kTintStore\)/,
  'the tint keeps the resting color, not the color shown during a press');

const host = await lvglHost(root);
if (!host) {
  console.log('SKIP: tile tint press restore needs LVGL and a host compiler');
  process.exit(0);
}
const out = path.join(root, 'build/tests/tile-tint-press-restore');
fs.mkdirSync(out, {recursive: true});
const fonts = read('src/tiles/runtime/tile_renderer_fonts.h').replace(/^#include.*$/gm, '')
  .replace('#pragma once', '').replaceAll('constexpr lv_coord_t', 'constexpr long');
const tintStore = source.match(/constexpr lv_style_selector_t kTintStore = [^;]*;/)[0];
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
${read('src/tiles/runtime/tile_icon_disc.h').replace(/^#.*$/gm, '')}
#include "src/tiles/config/tile_tint.h"
static uint32_t tileDefaultBgColor() { return 0x1A1A1A; }
namespace tile_icon_source {
${tintStore}
${fn('apply_card_background')}
${fn('set_tile_tint')}
${fn('clear_tile_tint')}
}
static uint32_t rgb(lv_color_t c) { return lv_color_to_u32(c) & 0xFFFFFF; }
static uint32_t local(lv_obj_t* obj, lv_style_selector_t selector) {
  lv_style_value_t v;
  return lv_obj_get_local_style_prop(obj, LV_STYLE_BG_COLOR, &v, selector) == LV_STYLE_RES_FOUND ? rgb(v.color) : 0xFFFFFFFF;
}
int main() {
  lv_init();
  static uint32_t px[64 * 64];
  lv_display_t* display = lv_display_create(64, 64);
  lv_display_set_color_format(display, LV_COLOR_FORMAT_XRGB8888);
  lv_display_set_buffers(display, px, nullptr, sizeof(px), LV_DISPLAY_RENDER_MODE_FULL);
  // A Switch card as the renderer builds it: own color at rest, 0x10 lighter
  // while pressed.
  const uint32_t rest = 0x181818, pressed = 0x282828, light = 0x039DD6;
  lv_obj_t* card = lv_obj_create(lv_screen_active());
  lv_obj_remove_style_all(card);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(rest), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(card, lv_color_hex(pressed), LV_PART_MAIN | LV_STATE_PRESSED);
  const uint32_t tint = tile_tint::background(tileDefaultBgColor(), light, 20);
  for (int round = 1; round <= 3; ++round) {
    // Tap on: the optimistic state tints the still pressed card, then the
    // Home Assistant echo tints it again at rest.
    lv_obj_add_state(card, LV_STATE_PRESSED);
    tile_icon_source::set_tile_tint(card, light, 20);
    lv_obj_remove_state(card, LV_STATE_PRESSED);
    tile_icon_source::set_tile_tint(card, light, 20);
    if (local(card, LV_PART_MAIN | LV_STATE_DEFAULT) != tint) {
      std::printf("FAIL round %d: on shows #%06X, expected #%06X\n", round,
                  (unsigned)local(card, LV_PART_MAIN | LV_STATE_DEFAULT), (unsigned)tint);
      return 1;
    }
    // Tap off: the grey icon clears the tint while the card is pressed.
    lv_obj_add_state(card, LV_STATE_PRESSED);
    tile_icon_source::clear_tile_tint(card);
    lv_obj_remove_state(card, LV_STATE_PRESSED);
    if (local(card, LV_PART_MAIN | LV_STATE_DEFAULT) != rest ||
        local(card, LV_PART_MAIN | LV_STATE_PRESSED) != pressed ||
        rgb(lv_obj_get_style_bg_color(card, LV_PART_MAIN)) != rest) {
      std::printf("FAIL round %d: off rests at #%06X (pressed #%06X), expected #%06X (#%06X)\n", round,
                  (unsigned)local(card, LV_PART_MAIN | LV_STATE_DEFAULT),
                  (unsigned)local(card, LV_PART_MAIN | LV_STATE_PRESSED), (unsigned)rest, (unsigned)pressed);
      return 1;
    }
  }
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
console.log('From icon tint: a tile tapped on and off returns to its own resting color every time.');
