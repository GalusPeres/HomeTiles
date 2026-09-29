// Footer controls of the history popups (7D/24H/Today, date and day pills):
// glass without the opening tile's "Circle in icon color", the popup hue made
// lighter with it; white text stays readable (regression: solid white pills
// with text cut out in the popup color in every popup).
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';
import {lvglHost} from '../../lib/lvgl-host.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = (p) => fs.readFileSync(path.join(root, p), 'utf8').replace(/\r\n/g, '\n');
const fn = (source, name) => {
  const found = cppFunctionDefinitions(source).find((f) => f.name === name);
  assert(found, name);
  return found.source;
};

// Every history popup styles its toggles and pills through popup_nav_style.
const sensor = read('src/ui/popups/sensor/sensor_popup.cpp');
const energy = read('src/ui/popups/energy/energy_popup.cpp');
const weather = read('src/ui/popups/weather/weather_popup.cpp');
for (const [source, name] of [[sensor, 'style_range_button'], [energy, 'style_period_button'],
  [weather, 'style_mode_button'], [weather, 'style_header_action_button']]) {
  const body = fn(source, name);
  assert.match(body, /popup_nav_style::style_toggle\(btn, /, name);
  assert.doesNotMatch(body, /lv_color_white\(\), selector/, `${name} has no white fill`);
}
const cardColor = fn(weather, 'apply_card_color');
assert.match(cardColor, /popup_nav_style::style_pill\(ctx->week_range_pill, ctx->week_range_label/);
assert.match(cardColor, /popup_nav_style::style_pill\(ctx->detail_title_pill, ctx->detail_title_label/);
assert.match(fn(weather, 'weather_popup_follow_tile_color'), /apply_card_color\(ctx, color\);\s*[^}]*update_mode_buttons\(ctx\);/);
assert.match(fn(sensor, 'sensor_popup_follow_tile_color'), /update_range_buttons\(ctx\);/);
assert.match(fn(energy, 'energy_popup_follow_tile_color'), /update_period_buttons\(ctx\);/);
// The tile that opens the popup decides, before and after the shell took over.
assert.match(fn(read('src/ui/popups/popup_shell.cpp'), 'popup_shell_tinted_controls'),
  /g_next_disc\.from_tile \? g_next_disc : shell\.disc;\s*return disc\.from_tile && disc\.glow;/);

const host = await lvglHost(root);
if (!host) {
  console.log('Popup footer controls use popup_nav_style; SKIP: rendering needs LVGL and a host compiler');
  process.exit(0);
}

const strip = (s) => s.replace(/^#include.*$/gm, '').replaceAll('#pragma once', '');
const out = path.join(root, 'build/tests/popup-nav-style');
fs.mkdirSync(out, {recursive: true});
const cpp = String.raw`
#include <lvgl.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>
${strip(read('src/tiles/config/tile_tint.h'))}
static bool g_tinted = false;
bool popup_shell_tinted_controls() { return g_tinted; }
${strip(read('src/ui/popups/popup_nav_style.h'))}
using namespace popup_nav_style;
static void hsl(uint32_t c, double& h, double& s, double& l) {
  const double r = ((c >> 16) & 255) / 255.0, g = ((c >> 8) & 255) / 255.0, b = (c & 255) / 255.0;
  const double hi = std::fmax(r, std::fmax(g, b)), lo = std::fmin(r, std::fmin(g, b));
  l = (hi + lo) / 2; h = 0; s = 0;
  if (hi > lo) {
    const double d = hi - lo;
    s = l > 0.5 ? d / (2 - hi - lo) : d / (hi + lo);
    h = hi == r ? (g - b) / d + (g < b ? 6 : 0) : hi == g ? (b - r) / d + 2 : (r - g) / d + 4;
    h *= 60;
  }
}
int main() {
  lv_init();
  std::vector<uint32_t> px(64 * 64);
  lv_display_t* display = lv_display_create(64, 64);
  lv_display_set_color_format(display, LV_COLOR_FORMAT_XRGB8888);
  lv_display_set_buffers(display, px.data(), nullptr, px.size() * 4, LV_DISPLAY_RENDER_MODE_FULL);
  lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t*, uint8_t*) { lv_display_flush_ready(d); });
  int ok = 1;
  auto check = [&](bool v, const char* what) { if (!v) { std::printf("FAIL %s\n", what); ok = 0; } };
  // Glass: white mixed into the popup color; the selected fill is brighter.
  check(glass(0x1C1C1C, Fill::Info) == tile_tint::mix(0x1C1C1C, 0xFFFFFF, 14), "glass info is 14 % white");
  check(glass(0x1C1C1C, Fill::Selected) == tile_tint::mix(0x1C1C1C, 0xFFFFFF, 30), "glass selected is 30 % white");
  // Hue: the popup hue, lighter; grey stays grey.
  const uint32_t gold = 0x45391B;
  double h0, s0, l0, h1, s1, l1, h2, s2, l2;
  hsl(gold, h0, s0, l0); hsl(hue_lighter(gold, Fill::Info), h1, s1, l1); hsl(hue_lighter(gold, Fill::Selected), h2, s2, l2);
  check(std::fabs(h1 - h0) < 3 && std::fabs(h2 - h0) < 3, "hue kept");
  check(l1 > l0 + 0.07 && l2 > l1 + 0.12, "info and selected get lighter");
  check(s1 >= s0 - 0.01, "saturation kept");
  const uint32_t grey = hue_lighter(0x1C1C1C, Fill::Selected);
  check(((grey >> 16) & 255) == ((grey >> 8) & 255) && ((grey >> 8) & 255) == (grey & 255), "grey stays grey");
  // White text stays readable on light popup colors.
  for (uint32_t base : {0xFFD700u, 0xF4F4EFu, 0x8BC34Au}) {
    check(tile_tint::white_contrast(fill_rgb(base, false, Fill::Selected)) >= 3.0, "readable glass");
    check(tile_tint::white_contrast(fill_rgb(base, true, Fill::Selected)) >= 3.0, "readable hue");
  }
  // A real toggle: selected is filled, unselected only shows its label, and
  // "Circle in icon color" switches glass to the popup hue.
  lv_obj_t* btn = lv_button_create(lv_screen_active());
  lv_obj_t* label = lv_label_create(btn);
  style_toggle(btn, label, lv_color_hex(gold), true);
  check(lv_obj_get_style_bg_opa(btn, LV_PART_MAIN) == LV_OPA_COVER, "selected filled");
  check((lv_color_to_u32(lv_obj_get_style_bg_color(btn, LV_PART_MAIN)) & 0xFFFFFF) == fill_rgb(gold, false, Fill::Selected), "glass fill");
  check((lv_color_to_u32(lv_obj_get_style_text_color(label, LV_PART_MAIN)) & 0xFFFFFF) == 0xFFFFFF, "white text");
  g_tinted = true;
  style_toggle(btn, label, lv_color_hex(gold), true);
  check((lv_color_to_u32(lv_obj_get_style_bg_color(btn, LV_PART_MAIN)) & 0xFFFFFF) == fill_rgb(gold, true, Fill::Selected), "hue fill");
  style_toggle(btn, label, lv_color_hex(gold), false);
  check(lv_obj_get_style_bg_opa(btn, LV_PART_MAIN) == LV_OPA_TRANSP, "unselected transparent");
  lv_obj_add_state(btn, LV_STATE_PRESSED);
  check(lv_obj_get_style_bg_opa(btn, LV_PART_MAIN) == LV_OPA_COVER &&
        (lv_color_to_u32(lv_obj_get_style_bg_color(btn, LV_PART_MAIN)) & 0xFFFFFF) == fill_rgb(gold, true, Fill::Info), "pressed shows info fill");
  lv_obj_t* pill = lv_obj_create(lv_screen_active());
  lv_obj_t* pill_label = lv_label_create(pill);
  g_tinted = false;
  style_pill(pill, pill_label, lv_color_hex(gold));
  check((lv_color_to_u32(lv_obj_get_style_bg_color(pill, LV_PART_MAIN)) & 0xFFFFFF) == fill_rgb(gold, false, Fill::Info), "pill info fill");
  std::printf("%s\n", ok ? "OK" : "FAILED");
  return ok ? 0 : 1;
}
`;
const source = path.join(out, 'test.cpp');
const binary = path.join(out, process.platform === 'win32' ? 'test.exe' : 'test');
fs.writeFileSync(source, cpp);
let result = spawnSync(host.cxx, [...host.flags, '-std=c++17', source, host.archive, '-o', binary], {encoding: 'utf8'});
assert.equal(result.status, 0, result.stdout + result.stderr);
result = spawnSync(binary, [], {encoding: 'utf8'});
assert.equal(result.status, 0, result.stdout + result.stderr);
console.log('Popup footer controls: glass without, popup hue with "Circle in icon color"; white text readable.');
