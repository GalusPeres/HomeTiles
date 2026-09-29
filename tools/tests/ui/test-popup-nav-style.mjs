// Footer controls of the history popups (7D/24H/Today, date and day pills)
// and the pressed close button match the header icon disc: the selected
// control has exactly the disc fill (white, or the icon color with "Circle in
// icon color", at the Glow strength), the info pill half of it, and all
// labels stay white (regression: solid white pills brighter than the disc,
// and a white pill with text cut out in the popup color).
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

// Every history popup styles its toggles and pills through popup_nav_style
// with its card and header icon color.
const sensor = read('src/ui/popups/sensor/sensor_popup.cpp');
const energy = read('src/ui/popups/energy/energy_popup.cpp');
const weather = read('src/ui/popups/weather/weather_popup.cpp');
for (const [source, name] of [[sensor, 'style_range_button'], [energy, 'style_period_button'],
  [weather, 'style_mode_button'], [weather, 'style_header_action_button']]) {
  const body = fn(source, name);
  assert.match(body, /popup_nav_style::style_toggle\(btn, [^;]*icon[^;]*\)/, name);
  assert.doesNotMatch(body, /lv_color_white\(\), selector|LV_OPA_COVER, 0\)/, `${name} has no own fill`);
}
const cardColor = fn(weather, 'apply_card_color');
assert.match(cardColor, /popup_nav_style::style_pill\(ctx->week_range_pill, ctx->week_range_label, [^;]*header_icon_color\(ctx\)\)/);
assert.match(cardColor, /popup_nav_style::style_pill\(ctx->detail_title_pill, ctx->detail_title_label, [^;]*header_icon_color\(ctx\)\)/);
// The weather header icon color is set before the pills are styled.
assert.match(fn(weather, 'apply_init_to_context'),
  /lv_obj_set_style_text_color\(ctx->icon_label, lv_color_hex\(init\.icon_color\), 0\);\s*apply_card_color\(ctx, init\.bg_color\);/);
assert.match(fn(weather, 'weather_popup_follow_tile_color'), /apply_card_color\(ctx, color\);\s*[^}]*update_mode_buttons\(ctx\);/);
assert.match(fn(sensor, 'apply_popup_icon_color'), /lv_obj_set_style_text_color\(ctx->icon_label, color, 0\);\s*[^}]*update_range_buttons\(ctx\);/);
assert.match(fn(sensor, 'sensor_popup_follow_tile_color'), /update_range_buttons\(ctx\);/);
assert.match(fn(energy, 'energy_popup_follow_tile_color'), /update_period_buttons\(ctx\);/);
// One disc fill for the header disc, the footer controls and the close button.
const shell = read('src/ui/popups/popup_shell.cpp');
const tint = fn(shell, 'apply_header_disc_tint');
assert.match(tint, /disc_fill\(options, card, rgb, color, fill_opa\);/);
assert.match(tint, /lv_obj_set_style_bg_color\(shell\.close, color, LV_STATE_PRESSED\);\s*lv_obj_set_style_bg_opa\(shell\.close, fill_opa, LV_STATE_PRESSED\);/);
assert.match(fn(shell, 'popup_shell_disc_fill'),
  /g_next_disc\.from_tile \? g_next_disc : shell\.disc;\s*disc_fill\(options,/);

const host = await lvglHost(root);
if (!host) {
  console.log('Popup footer controls match the icon disc; SKIP: rendering needs LVGL and a host compiler');
  process.exit(0);
}

const strip = (s) => s.replace(/^#include.*$/gm, '').replaceAll('#pragma once', '');
const out = path.join(root, 'build/tests/popup-nav-style');
fs.mkdirSync(out, {recursive: true});
const cpp = String.raw`
#include <lvgl.h>
#include <cstdint>
#include <cstdio>
static lv_color_t g_disc_color = lv_color_white();
static lv_opa_t g_disc_opa = 40;
static uint32_t g_card = 0, g_icon = 0;
void popup_shell_disc_fill(uint32_t card, uint32_t icon, lv_color_t& color, lv_opa_t& opa) {
  g_card = card; g_icon = icon; color = g_disc_color; opa = g_disc_opa;
}
${strip(read('src/ui/popups/popup_nav_style.h'))}
using namespace popup_nav_style;
static uint32_t rgb(lv_color_t c) { return lv_color_to_u32(c) & 0xFFFFFF; }
int main() {
  lv_init();
  static uint32_t px[64 * 64];
  lv_display_t* display = lv_display_create(64, 64);
  lv_display_set_color_format(display, LV_COLOR_FORMAT_XRGB8888);
  lv_display_set_buffers(display, px, nullptr, sizeof(px), LV_DISPLAY_RENDER_MODE_FULL);
  int ok = 1;
  auto check = [&](bool v, const char* what) { if (!v) { std::printf("FAIL %s\n", what); ok = 0; } };
  const lv_color_t gold = lv_color_hex(0x45391B), sun = lv_color_hex(0xFFB224);
  lv_obj_t* btn = lv_button_create(lv_screen_active());
  lv_obj_t* label = lv_label_create(btn);
  // Selected: exactly the disc fill of this card and icon, full white text.
  style_toggle(btn, label, gold, sun, true);
  check(g_card == 0x45391B && g_icon == 0xFFB224, "asks for the disc of this card and icon");
  check(lv_obj_get_style_bg_opa(btn, LV_PART_MAIN) == 40 && rgb(lv_obj_get_style_bg_color(btn, LV_PART_MAIN)) == 0xFFFFFF,
        "selected has the neutral disc fill");
  check(lv_obj_get_style_text_opa(label, LV_PART_MAIN) == LV_OPA_COVER, "selected text full white");
  g_disc_color = sun; g_disc_opa = 54;
  style_toggle(btn, label, gold, sun, true);
  check(lv_obj_get_style_bg_opa(btn, LV_PART_MAIN) == 54 && rgb(lv_obj_get_style_bg_color(btn, LV_PART_MAIN)) == 0xFFB224,
        "selected has the tinted disc fill");
  // Unselected: no fill, white label; pressing shows the disc fill.
  style_toggle(btn, label, gold, sun, false);
  check(lv_obj_get_style_bg_opa(btn, LV_PART_MAIN) == LV_OPA_TRANSP, "unselected has no fill");
  check(lv_obj_get_style_text_opa(label, LV_PART_MAIN) == LV_OPA_COVER &&
        (rgb(lv_obj_get_style_text_color(label, LV_PART_MAIN)) == 0xFFFFFF), "unselected text white");
  lv_obj_add_state(btn, LV_STATE_PRESSED);
  check(lv_obj_get_style_bg_opa(btn, LV_PART_MAIN) == 54, "pressed shows the disc fill");
  lv_obj_remove_state(btn, LV_STATE_PRESSED);
  // Info pill: half the disc fill, white text.
  lv_obj_t* pill = lv_obj_create(lv_screen_active());
  lv_obj_t* pill_label = lv_label_create(pill);
  style_pill(pill, pill_label, gold, sun);
  check(lv_obj_get_style_bg_opa(pill, LV_PART_MAIN) == 27 && rgb(lv_obj_get_style_bg_color(pill, LV_PART_MAIN)) == 0xFFB224,
        "pill has half the disc fill");
  check(lv_obj_get_style_text_opa(pill_label, LV_PART_MAIN) == LV_OPA_COVER, "pill text white");
  // A Glow strength near zero keeps the selection visible.
  g_disc_opa = 0;
  style_toggle(btn, label, gold, sun, true);
  check(lv_obj_get_style_bg_opa(btn, LV_PART_MAIN) == kMinSelectedOpa, "minimum selection fill");
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
console.log('Popup footer controls and the close button match the header icon disc.');
