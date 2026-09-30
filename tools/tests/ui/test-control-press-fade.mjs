// A pressed popup control shows exactly the control color and fades to it
// from the card (regression b127): with opaque control colors the default
// theme's press fade ran from its teal resting color and flashed green, its
// black recolor darkened every press (a held "Today" stayed darker than the
// selected "7D"), a PIN key no longer lit up, and a button on a control
// surface pressed invisibly. Runs the production popup_nav_style.h on the
// panels' RGB565 format with the device theme and its transitions.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {lvglHost} from '../../lib/lvgl-host.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const host = await lvglHost(root);
if (!host) {
  console.log('SKIP: Control press fade needs LVGL and a host compiler');
  process.exit(0);
}
const out = path.join(root, 'build/tests/control-press-fade');
fs.mkdirSync(out, {recursive: true});
const cpp = String.raw`
#include <lvgl.h>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include "src/ui/shared/tone_color.h"
// The popup control fill as popup_shell.cpp gives it for a From icon tile.
void popup_shell_control_fill(uint32_t card, uint32_t icon, lv_color_t& color, lv_opa_t& opa, bool* tinted) {
  const tone_color::Fill f = tone_color::fill(card, icon, true, 25);
  color = lv_color_hex(f.control_color); opa = f.control_opa; if (tinted) *tinted = true;
}
void popup_shell_control_raised_fill(uint32_t card, uint32_t icon, lv_color_t& color, lv_opa_t& opa) {
  const tone_color::Fill f = tone_color::fill(card, icon, true, 25);
  color = lv_color_hex(f.raised_color); opa = f.control_opa;
}
#include "src/ui/popups/popup_nav_style.h"
static uint16_t fb[240 * 120];
static uint32_t px(int x, int y) {
  const uint16_t c = fb[y * 240 + x];
  const unsigned r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
  return ((r * 527 + 23) >> 6) << 16 | ((g * 259 + 33) >> 6) << 8 | ((b * 527 + 23) >> 6);
}
static uint32_t as565(uint32_t c) {
  const unsigned r = (c >> 19) & 31, g = (c >> 10) & 63, b = (c >> 3) & 31;
  return ((r * 527 + 23) >> 6) << 16 | ((g * 259 + 33) >> 6) << 8 | ((b * 527 + 23) >> 6);
}
static int ch(uint32_t c, int s) { return (c >> s) & 255; }
static lv_display_t* display;
static void step(int ms) { for (int t = 0; t < ms; t += 10) { lv_tick_inc(10); lv_timer_handler(); lv_refr_now(display); } }
static int fail(const char* what, uint32_t got, uint32_t want) {
  std::printf("FAIL %s: #%06X, expected #%06X\n", what, (unsigned)got, (unsigned)want); return 1;
}
int main() {
  lv_init();
  display = lv_display_create(240, 120);
  lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(display, fb, nullptr, sizeof(fb), LV_DISPLAY_RENDER_MODE_DIRECT);
  lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t*, uint8_t*) { lv_display_flush_ready(d); });
  // The device theme: teal accent, light mode, 80 ms transitions.
  lv_display_set_theme(display, lv_theme_default_init(display, lv_color_hex(0x26A69A), lv_color_hex(0xC14444), false,
                                                      LV_FONT_DEFAULT));
  const uint32_t card_rgb = 0x483817, icon_rgb = 0xFFC107;
  lv_obj_t* card = lv_obj_create(lv_screen_active());
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, 240, 120);
  lv_obj_set_style_bg_color(card, lv_color_hex(card_rgb), 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  auto button = [&](int x, lv_obj_t* parent) {
    lv_obj_t* b = lv_button_create(parent);
    lv_obj_set_size(b, 50, 50);
    lv_obj_set_pos(b, x, 10);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
    return b;
  };
  const tone_color::Fill fill = tone_color::fill(card_rgb, icon_rgb, true, 25);
  const uint32_t card565 = as565(card_rgb), control565 = as565(fill.control_color), raised565 = as565(fill.raised_color);
  // A press-only button (close, Media previous/next, Weather arrows).
  lv_obj_t* press = button(10, card);
  popup_nav_style::style_press(press, lv_color_hex(card_rgb), lv_color_hex(icon_rgb));
  // A toggle (7D/24H/Today), selected and not.
  lv_obj_t* selected = button(70, card);
  popup_nav_style::style_toggle(selected, nullptr, lv_color_hex(card_rgb), lv_color_hex(icon_rgb), true);
  lv_obj_t* toggle = button(130, card);
  popup_nav_style::style_toggle(toggle, nullptr, lv_color_hex(card_rgb), lv_color_hex(icon_rgb), false);
  // A button on a control surface (date arrows on their field).
  lv_obj_t* surface = lv_obj_create(card);
  lv_obj_remove_style_all(surface);
  lv_obj_set_size(surface, 50, 50);
  lv_obj_set_pos(surface, 190, 10);
  lv_obj_set_style_bg_color(surface, lv_color_hex(fill.control_color), 0);
  lv_obj_set_style_bg_opa(surface, LV_OPA_COVER, 0);
  lv_obj_t* arrow = button(0, surface);
  lv_obj_set_pos(arrow, 0, 0);
  popup_nav_style::style_press_raised(arrow, lv_color_hex(card_rgb), lv_color_hex(icon_rgb));
  lv_refr_now(display);
  if (px(35, 35) != card565) return fail("press button at rest", px(35, 35), card565);
  if (px(95, 35) != control565) return fail("selected toggle", px(95, 35), control565);
  if (px(155, 35) != card565) return fail("toggle at rest", px(155, 35), card565);
  if (px(215, 35) != control565) return fail("surface", px(215, 35), control565);
  // Every frame of a held press lies between the card and the control color
  // (no teal, no black), and it ends exactly on the control color.
  auto hold = [&](lv_obj_t* obj, int x, uint32_t from, uint32_t to, const char* what) {
    lv_obj_add_state(obj, LV_STATE_PRESSED);
    for (int t = 0; t < 200; t += 10) {
      step(10);
      const uint32_t c = px(x, 35);
      for (int s : {16, 8, 0}) {
        const int lo = ch(from, s) < ch(to, s) ? ch(from, s) : ch(to, s);
        const int hi = ch(from, s) > ch(to, s) ? ch(from, s) : ch(to, s);
        if (ch(c, s) < lo - 8 || ch(c, s) > hi + 8) { fail(what, c, to); std::exit(1); }
      }
    }
    if (px(x, 35) != to) { fail(what, px(x, 35), to); std::exit(1); }
    lv_obj_remove_state(obj, LV_STATE_PRESSED);
    step(300);
    if (px(x, 35) != from) { fail(what, px(x, 35), from); std::exit(1); }
  };
  hold(press, 35, card565, control565, "press button");
  hold(toggle, 155, card565, control565, "unselected toggle (Today)");
  hold(selected, 95, control565, control565, "selected toggle");
  if (raised565 == control565) return fail("raised differs from the control", raised565, control565);
  hold(arrow, 215, control565, raised565, "button on a surface");
  std::printf("OK\n");
  return 0;
}
`;
const src = path.join(out, 'test.cpp');
const binary = path.join(out, process.platform === 'win32' ? 'test.exe' : 'test');
fs.writeFileSync(src, cpp);
let result = spawnSync(host.cxx, [...host.flags, '-std=c++17', '-I', root, src, host.archive, '-o', binary], {encoding: 'utf8'});
assert.equal(result.status, 0, result.stdout + result.stderr);
result = spawnSync(binary, [], {encoding: 'utf8'});
assert.equal(result.status, 0, result.stdout + result.stderr);
console.log('Control press: fades from the card to the exact control color, no theme tint or darkening; raised on surfaces');
