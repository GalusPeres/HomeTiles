// Footer controls of the history popups (7D/24H/Today, date and day pills)
// and the pressed close button share one control fill: the selected control
// has exactly that fill (the circle's color, tone_color::fill), a press shows
// that same color without theme darkening, the info pill and the slider
// track the same (regression: a half fill looked darker than 7D), and all
// labels stay white
// (regression: solid white pills brighter than the disc, and a white pill with
// text cut out in the popup color). test-popup-shell-lvgl.mjs runs the fill.
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
// One control fill for the footer controls and the pressed close button.
const shell = read('src/ui/popups/popup_shell.cpp');
const tint = fn(shell, 'apply_header_disc_tint');
assert.match(tint, /const tone_color::Fill fill = header_fill\(options, card, rgb\);/);
assert.match(tint, /const tone_color::Fill controls = controls_fill\(options, card, rgb\);/);
assert.match(tint, /lv_obj_set_style_bg_color\(shell\.close, press, LV_STATE_PRESSED\);\s*lv_obj_set_style_bg_opa\(shell\.close, press_opa, LV_STATE_PRESSED\);\s*lv_obj_set_style_color_filter_opa\(shell\.close, LV_OPA_TRANSP, LV_STATE_PRESSED\);/);
assert.match(fn(shell, 'control_fill'), /const tone_color::Fill fill = controls_fill\(options, card, rgb\);\s*color = lv_color_hex\(fill\.control_color\);\s*opa = fill\.control_opa;/);
// The agreed table: the controls take the circle color only when the popup
// shows the tile color "From icon"; the Climate, Light and Cover popups
// (forget_popup_source) keep neutral controls.
// Controls take the circle color whenever it is tinted (user 2026-10-01).
assert.match(fn(shell, 'controls_fill'), /return header_fill\(options, card, rgb\);/);
const iconSource = read('src/tiles/runtime/tile_icon_source.cpp');
assert.match(fn(iconSource, 'forget_popup_source'), /pass_popup_disc\(obj, false\);/);
assert.match(fn(iconSource, 'popup_background'), /pass_popup_disc\(obj, true\);/);
assert.match(fn(shell, 'popup_shell_control_fill'),
  /g_next_disc\.from_tile \? g_next_disc : shell\.disc;\s*control_fill\(options,/);
// Media: previous, next and volume take the fill while pressed, both sliders
// the pill track and the control color; play keeps its white circle. Every
// open and state update restyles them with the card and header icon color.
const media = read('src/ui/popups/media/media_popup.cpp');
const mediaColors = fn(media, 'apply_control_colors');
assert.match(mediaColors, /lv_obj_t\* const labels\[\] = \{ctx->previous_label, ctx->next_label, ctx->volume_icon_label\};/);
assert.match(mediaColors, /popup_nav_style::style_press\(lv_obj_get_parent\(label\), popup, icon\);/);
assert.match(mediaColors, /popup_nav_style::style_slider\(ctx->seek_slider, popup, icon\);\s*popup_nav_style::style_slider\(ctx->volume_slider, popup, icon\);/);
assert.doesNotMatch(mediaColors, /play_pause/);
assert.match(fn(media, 'apply_init_to_context'), /update_cover\(ctx, init\.cover_dsc, init\.cover_hash\);\s*apply_control_colors\(ctx\);\s*apply_availability\(ctx\);\s*\}$/);

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
static bool g_tinted = true;
void popup_shell_control_fill(uint32_t card, uint32_t icon, lv_color_t& color, lv_opa_t& opa, bool* tinted) {
  g_card = card; g_icon = icon; color = g_disc_color; opa = g_disc_opa;
  if (tinted) *tinted = g_tinted;
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
  check(g_card == 0x45391B && g_icon == 0xFFB224, "asks for the control fill of this card and icon");
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
  check(lv_obj_get_style_color_filter_opa(btn, LV_PART_MAIN) == LV_OPA_TRANSP, "pressed is not darkened");
  lv_obj_remove_state(btn, LV_STATE_PRESSED);
  // Info pill: the same fill as a selected toggle, white text.
  lv_obj_t* pill = lv_obj_create(lv_screen_active());
  lv_obj_t* pill_label = lv_label_create(pill);
  style_pill(pill, pill_label, gold, sun);
  check(lv_obj_get_style_bg_opa(pill, LV_PART_MAIN) == 54 && rgb(lv_obj_get_style_bg_color(pill, LV_PART_MAIN)) == 0xFFB224,
        "pill has the toggle fill");
  check(lv_obj_get_style_text_opa(pill_label, LV_PART_MAIN) == LV_OPA_COVER, "pill text white");
  // Media previous, next and volume: the control fill only while pressed.
  lv_style_value_t v;
  auto local = [&](lv_obj_t* obj, lv_style_prop_t prop, lv_style_selector_t selector) {
    return lv_obj_get_local_style_prop(obj, prop, &v, selector) == LV_STYLE_RES_FOUND;
  };
  lv_obj_t* media_btn = lv_button_create(lv_screen_active());
  style_press(media_btn, gold, sun);
  check(local(media_btn, LV_STYLE_BG_OPA, LV_PART_MAIN | LV_STATE_PRESSED) && v.num == 54, "media press opacity");
  check(local(media_btn, LV_STYLE_BG_COLOR, LV_PART_MAIN | LV_STATE_PRESSED) && rgb(v.color) == 0xFFB224,
        "media press color");
  check(local(media_btn, LV_STYLE_COLOR_FILTER_OPA, LV_PART_MAIN | LV_STATE_PRESSED) && v.num == LV_OPA_TRANSP,
        "media press is not darkened");
  // Media sliders: the unused track like a pill, the used part in the icon
  // color while the controls take its hue (else white), the knob untouched.
  lv_obj_t* slider = lv_slider_create(lv_screen_active());
  lv_obj_set_style_bg_color(slider, lv_color_white(), LV_PART_KNOB);
  style_slider(slider, gold, sun);
  check(local(slider, LV_STYLE_BG_OPA, LV_PART_MAIN) && v.num == 54, "slider track has the toggle fill");
  check(local(slider, LV_STYLE_BG_COLOR, LV_PART_MAIN) && rgb(v.color) == 0xFFB224, "slider track color");
  check(local(slider, LV_STYLE_BG_OPA, LV_PART_INDICATOR) && v.num == LV_OPA_COVER, "slider used part opaque");
  check(local(slider, LV_STYLE_BG_COLOR, LV_PART_INDICATOR) && rgb(v.color) == 0xFFB224, "slider used part icon color");
  check(local(slider, LV_STYLE_BG_COLOR, LV_PART_KNOB) && rgb(v.color) == 0xFFFFFF, "slider knob stays white");
  g_disc_color = lv_color_white(); g_disc_opa = 24; g_tinted = false;
  style_slider(slider, gold, sun);
  check(local(slider, LV_STYLE_BG_OPA, LV_PART_MAIN) && v.num == 24, "neutral track has the toggle fill");
  check(local(slider, LV_STYLE_BG_COLOR, LV_PART_INDICATOR) && rgb(v.color) == 0xFFFFFF, "neutral used part white");
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
console.log('Popup footer controls take the shared control fill.');
