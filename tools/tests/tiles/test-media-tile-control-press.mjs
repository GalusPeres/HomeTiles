// Media tile previous and next press like the popup controls: exactly the
// circle's color (tone_color::fill; the icon hue with "Circle in icon color",
// else neutral), opaque, never below the minimum step, with no theme
// darkening; play keeps its white circle. A card press recolors the circle
// and the resting pill for the pressed card. Runs the production
// refresh_controls() with the real disc tags and shared styles.
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

// Every icon color, tint and circle change reaches the buttons through the
// disc hook; the tile and popup controls share one minimum opacity.
assert.match(fn('on_icon_color'), /^void on_icon_color\(lv_obj_t\* disc\) \{\s*refresh_controls\(lv_obj_get_parent\(disc\)\);/);
assert.match(read('src/ui/shared/tone_color.h'), /inline constexpr uint8_t kControlMinOpa = 32;/);
assert.match(read('src/ui/shared/ui_surface_style.cpp'),
  /lv_style_set_bg_opa\(&entry\.style, control_fill_opa\(i == 1\)\);\s*lv_obj_report_style_change\(&entry\.style\);/,
  'a Circle strength change updates the shared press opacity');

const host = await lvglHost(root);
if (!host) {
  console.log('SKIP: Media tile control press needs LVGL and a host compiler');
  process.exit(0);
}
const out = path.join(root, 'build/tests/media-tile-control-press');
fs.mkdirSync(out, {recursive: true});
const fonts = read('src/tiles/runtime/tile_renderer_fonts.h').replace(/^#include.*$/gm, '')
  .replace('#pragma once', '').replaceAll('constexpr lv_coord_t', 'constexpr long');
const tintStore = source.match(/constexpr lv_style_selector_t kTintStore = [^;]*;/)[0];
const coverStore = source.match(/constexpr lv_style_selector_t kCoverStore = [^;]*;/)[0];
const cpp = String.raw`
#include <lvgl.h>
#include <atomic>
#include <cassert>
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
${read('src/tiles/runtime/tile_icon_disc.h').replace(/^#.*$/gm, '')}
#include "src/tiles/config/tile_tint.h"
namespace tile_icon_source {
${tintStore}
${coverStore}
${fn('cover_color')}
${fn('cover_tile')}
${fn('cover_tints')}
${fn('icon_fill_marker')}
${fn('set_icon_fill_marker')}
${fn('find_disc')}
${fn('disc_icon_rgb')}
${fn('style_controls')}
${fn('follow_card_press')}
${fn('refresh_controls')}
}
static uint32_t rgb(lv_color_t c) { return lv_color_to_u32(c) & 0xFFFFFF; }
int main() {
  lv_init();
  static uint32_t px[64 * 64];
  lv_display_t* display = lv_display_create(64, 64);
  lv_display_set_color_format(display, LV_COLOR_FORMAT_XRGB8888);
  lv_display_set_buffers(display, px, nullptr, sizeof(px), LV_DISPLAY_RENDER_MODE_FULL);
  // A card with its round disc, the icon behind it and previous (marked) and
  // play (unmarked) buttons.
  lv_obj_t* card = lv_obj_create(lv_screen_active());
  lv_obj_remove_style_all(card);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_t* disc = lv_obj_create(card);
  lv_obj_t* icon = lv_label_create(card);
  lv_obj_t* previous = lv_obj_create(card);
  lv_obj_t* play = lv_obj_create(card);
  lv_obj_remove_style_all(previous);
  lv_obj_remove_style_all(play);
  tile_icon_disc::mark_control(previous);
  assert(tile_icon_disc::is_control(previous) && !tile_icon_disc::is_control(play) && !tile_icon_disc::is_disc(previous));
  auto pressed = [&](lv_obj_t* obj, uint32_t& color, lv_opa_t& opa) {
    lv_obj_add_state(obj, LV_STATE_PRESSED);
    color = rgb(lv_obj_get_style_bg_color(obj, LV_PART_MAIN));
    opa = lv_obj_get_style_bg_opa(obj, LV_PART_MAIN);
    lv_obj_remove_state(obj, LV_STATE_PRESSED);
  };
  // The controls take the circle color only with tile color From icon; with
  // Global or Custom they take the neutral step.
  // Media "From cover" (a cover tint with a cover color) tints them like
  // From icon; without a cover color or with a grey cover they stay neutral.
  struct Case { const char* what; uint32_t card, icon; bool glow; uint8_t fill; bool tinted;
                uint8_t cover_tile; bool cover_known; uint32_t cover; };
  const Case cases[] = {
    {"Global with the circle color", 0x1B1B1B, 0xC62828, true, 0, false},
    {"Custom in the icon's hue", 0x3E1717, 0xC62828, true, 0, false},
    {"From icon with the circle color", 0x482F10, 0xEF8402, true, 20, true},
    {"From icon without the circle color", 0x482F10, 0xEF8402, false, 20, false},
    {"From icon with a white icon", 0x303030, 0xFFFFFF, true, 20, false},
    {"From cover, icon from cover", 0x4A2A1F, 0xF2672E, true, 0, true, 20, true, 0xF2672E},
    {"From cover, white icon", 0x4A2A1F, 0xFFFFFF, true, 0, false, 20, true, 0xF2672E},
    {"From cover without a cover color", 0x1B1B1B, 0xF2672E, true, 0, false, 20, false, 0},
    {"From cover with a grey cover", 0x1B1B1B, 0xF2672E, true, 0, false, 20, true, 0x808080},
    {"Cover color without From cover", 0x1B1B1B, 0xF2672E, true, 0, false, 0, true, 0xF2672E},
  };
  for (const Case& c : cases) {
    lv_obj_set_style_bg_color(card, lv_color_hex(c.card), 0);
    lv_obj_set_style_text_color(icon, lv_color_hex(c.icon), 0);
    tile_icon_disc::set_tag(disc, tile_icon_disc::Mode::On, c.glow);
    tile_icon_source::set_icon_fill_marker(card, c.fill);
    if (c.cover_tile) lv_obj_set_style_bg_opa(card, c.cover_tile, tile_icon_source::kCoverStore);
    else lv_obj_remove_local_style_prop(card, LV_STYLE_BG_OPA, tile_icon_source::kCoverStore);
    if (c.cover_known) lv_obj_set_style_bg_color(card, lv_color_hex(c.cover), tile_icon_source::kCoverStore);
    else lv_obj_remove_local_style_prop(card, LV_STYLE_BG_COLOR, tile_icon_source::kCoverStore);
    tile_icon_source::refresh_controls(card);
    uint32_t color; lv_opa_t opa;
    pressed(previous, color, opa);
    const tone_color::Fill expected = tone_color::fill(c.card, c.icon, c.tinted, icon_glow::kDefault);
    if (color != expected.control_color || opa != expected.control_opa || opa != LV_OPA_COVER) {
      std::printf("FAIL %s: #%06X @%d, expected #%06X @%d\n", c.what, (unsigned)color, opa,
                  (unsigned)expected.control_color, expected.control_opa);
      return 1;
    }
    // On screen the press shows the control color over the card.
    const uint32_t shown = tone_color::blend(c.card, color, opa);
    if (shown != expected.control) { std::printf("FAIL %s shown #%06X\n", c.what, (unsigned)shown); return 1; }
    { lv_style_value_t v;
      assert(lv_obj_get_local_style_prop(previous, LV_STYLE_COLOR_FILTER_OPA, &v, LV_PART_MAIN | LV_STATE_PRESSED) ==
             LV_STYLE_RES_FOUND && v.num == LV_OPA_TRANSP && "No theme darkening on press");
      assert(lv_obj_get_local_style_prop(previous, LV_STYLE_RECOLOR_OPA, &v, LV_PART_MAIN | LV_STATE_PRESSED) ==
             LV_STYLE_RES_FOUND && v.num == LV_OPA_TRANSP && "No theme recolor on press");
      assert(lv_obj_get_local_style_prop(previous, LV_STYLE_BG_COLOR, &v, LV_PART_MAIN) == LV_STYLE_RES_FOUND &&
             rgb(v.color) == color && "The press fades from the same resting color, not black"); }
  }
  // A resting surface (the Climate target pill) takes the fill at rest, and
  // its nested - and + buttons take it while pressed.
  lv_obj_t* pill = lv_obj_create(card);
  lv_obj_remove_style_all(pill);
  tile_icon_disc::mark_surface(pill);
  lv_obj_t* plus = lv_obj_create(pill);
  lv_obj_remove_style_all(plus);
  tile_icon_disc::mark_control(plus);
  lv_obj_remove_local_style_prop(card, LV_STYLE_BG_OPA, tile_icon_source::kCoverStore);
  lv_obj_remove_local_style_prop(card, LV_STYLE_BG_COLOR, tile_icon_source::kCoverStore);
  lv_obj_set_style_bg_color(card, lv_color_hex(0x482F10), 0);
  lv_obj_set_style_text_color(icon, lv_color_hex(0xEF8402), 0);
  tile_icon_disc::set_tag(disc, tile_icon_disc::Mode::On, true);
  tile_icon_source::set_icon_fill_marker(card, 20);
  tile_icon_source::refresh_controls(card);
  const uint32_t pill_color = tone_color::fill(0x482F10, 0xEF8402, true, icon_glow::kDefault).control_color;
  if (rgb(lv_obj_get_style_bg_color(pill, LV_PART_MAIN)) != pill_color ||
      lv_obj_get_style_bg_opa(pill, LV_PART_MAIN) != LV_OPA_COVER) {
    std::printf("FAIL resting surface: #%06X @%d\n", (unsigned)rgb(lv_obj_get_style_bg_color(pill, LV_PART_MAIN)),
                lv_obj_get_style_bg_opa(pill, LV_PART_MAIN));
    return 1;
  }
  // The nested buttons press one control step above the pill, so the press
  // shows on it (the same color as the pill made the press invisible).
  { uint32_t color; lv_opa_t opa; pressed(plus, color, opa);
    const uint32_t raised = tone_color::fill(0x482F10, 0xEF8402, true, icon_glow::kDefault).raised_color;
    if (color != raised || raised == pill_color || opa != LV_OPA_COVER) {
      std::printf("FAIL nested press: #%06X\n", (unsigned)color); return 1; }
    lv_style_value_t v;
    assert(lv_obj_get_local_style_prop(plus, LV_STYLE_RECOLOR_OPA, &v, LV_PART_MAIN | LV_STATE_PRESSED) ==
           LV_STYLE_RES_FOUND && v.num == LV_OPA_TRANSP && "No theme recolor on press");
    assert(lv_obj_get_local_style_prop(plus, LV_STYLE_BG_COLOR, &v, LV_PART_MAIN) == LV_STYLE_RES_FOUND &&
           rgb(v.color) == raised && "The press fades from the same resting color"); }
  // Play is not touched.
  { lv_style_value_t v; assert(lv_obj_get_local_style_prop(play, LV_STYLE_BG_COLOR, &v, LV_PART_MAIN | LV_STATE_PRESSED) != LV_STYLE_RES_FOUND); }
  // A pressed card (6 % lighter) takes the circle and the resting pill along
  // with the theme's press timing: they fade with the card instead of jumping
  // ahead of it (b127-b130: the circle flashed bright on press and dark on
  // release), keep their step above it, and the buttons stay as they are.
  {
    const uint32_t pressed_card = 0x583F20;
    lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t*, uint8_t*) { lv_display_flush_ready(d); });
    lv_obj_set_size(card, 64, 64);
    lv_obj_set_size(disc, 20, 20);
    lv_obj_set_size(pill, 40, 20);
    lv_obj_set_pos(pill, 0, 30);
    lv_obj_set_style_bg_color(card, lv_color_hex(pressed_card), LV_PART_MAIN | LV_STATE_PRESSED);
    tile_icon_disc::fade_with_card(disc);  // as tile_icon_disc::create() does
    tile_icon_disc::apply_fill(disc);
    tile_icon_source::refresh_controls(card);
    tile_icon_disc::follow_card_states(card);
    tile_icon_disc::follow_card_states(card);
    tile_icon_disc::g_card_state_hook = &tile_icon_source::follow_card_press;
    lv_refr_now(display);  // rendered widgets run their transitions
    const uint32_t events = lv_obj_get_event_count(card);
    auto now = [&](lv_obj_t* obj) { return rgb(lv_obj_get_style_bg_color(obj, LV_PART_MAIN)); };
    auto step = [&](int ms) { for (int t = 0; t < ms; t += 10) { lv_tick_inc(10); lv_timer_handler(); } };
    const uint32_t rest_disc = now(disc);
    const tone_color::Fill up = tone_color::fill(0x482F10, 0xEF8402, true, icon_glow::kDefault);
    const tone_color::Fill down = tone_color::fill(pressed_card, 0xEF8402, true, icon_glow::kDefault);
    assert(rest_disc == up.disc_color && now(pill) == pill_color);
    uint32_t plus_color; lv_opa_t plus_opa; pressed(plus, plus_color, plus_opa);
    lv_obj_add_state(card, LV_STATE_PRESSED);
    step(10);
    assert(now(disc) != down.disc_color && "The circle fades with the card, it does not jump ahead");
    step(200);
    if (now(disc) != down.disc_color || now(pill) != down.control_color) {
      std::printf("FAIL card press: disc #%06X pill #%06X\n", (unsigned)now(disc), (unsigned)now(pill));
      return 1;
    }
    { lv_style_value_t v;
      assert(lv_obj_get_local_style_prop(plus, LV_STYLE_BG_COLOR, &v, LV_PART_MAIN | LV_STATE_PRESSED) == LV_STYLE_RES_FOUND &&
             rgb(v.color) == plus_color && "A card press leaves the buttons alone"); }
    lv_obj_remove_state(card, LV_STATE_PRESSED);
    step(50);
    assert(now(disc) == down.disc_color && now(pill) == down.control_color &&
           "Like the card, they wait for the theme's release delay");
    step(250);
    assert(now(disc) == rest_disc && "Release restores the circle");
    assert(now(pill) == pill_color && "Release restores the pill");
    assert(events == 1 && "The card follows its state once");
    tile_icon_disc::g_card_state_hook = nullptr;
  }
  // A Circle strength of 0 keeps the press visible at the minimum step.
  configManager.cfg.icon_glow = 0;
  ui_surface_style::request_icon_disc_refresh();
  ui_surface_style::process_pending_updates();
  tile_icon_source::refresh_controls(card);
  { uint32_t color; lv_opa_t opa; pressed(previous, color, opa);
    assert(opa == LV_OPA_COVER && color == tone_color::fill(0x482F10, 0xEF8402, true, 0).control_color); }
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
console.log('Media tile previous/next press by the popup control rule; play untouched.');
