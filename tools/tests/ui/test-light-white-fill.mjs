// Issue #130 (user 2026-10-26, "wie in HA + Ausfuellen"): a light shows Home
// Assistant's color in both themes (below 40 % saturation raised to 40 %,
// white a grey of value 225), so 4400 K fills like 4000 K; "Ausfuellen"
// fills a white light in the neutral grey in the dark theme too; a Switch
// tile of the light theme is filled only while on; the Light popup shows the
// tile without "Ausfuellen" (circle, controls and handle line from the
// header icon's color and the popup card). Device and Web Admin preview agree.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {extractDeliveredFunction} from '../../lib/admin-source.mjs';
import {lvglHost} from '../../lib/lvgl-host.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = (p) => fs.readFileSync(path.join(root, p), 'utf8').replace(/\r\n/g, '\n');

// Firmware rules.
const renderer = read('src/tiles/runtime/tile_renderer.cpp');
assert.match(renderer, /uint32_t switch_state_icon_color\(const SwitchState& state\) \{[\s\S]*?return tone_color::ha_light_color\(rgb\);\n\}/,
  'switch_state_icon_color: Home Assistant\'s light color in both themes');
assert.ok(renderer.includes('return tile.type == TILE_SWITCH ? tile_surface::kFollowTint : tile_surface::kFollowTint | tile_surface::kNeutralIdle;'),
  'a Switch tile of the light theme is filled only while on (no idle grey)');
const source = read('src/tiles/runtime/tile_icon_source.cpp');
assert.match(source, /tile_tint::Choice with_white\([^)]*\) \{\n  if \(choice\.percent \|\| !fill \|\| !disc \|\| !tile_icon_disc::icon_of\(disc\)\) return choice;\n  uint8_t base = 0, flags = 0;\n  if \(ui_theme::theme_light\(\) && /,
  'with_white: only the light theme needs a filled card rule; the dark theme fills white light too');
const popup = read('src/ui/popups/light/light_popup.cpp');
assert.ok(popup.includes('popup_nav_style::fill(popup_surface::card(ctx->card_bg), lv_color_hex(shown_icon_rgb(ctx, header_icon_rgb(ctx, icon_rgb))),'),
  'Light popup controls use the header icon\'s color, like its circle');
assert.ok(popup.includes('static lv_color_t brightness_dash_color(const LightPopupContext* ctx) { return popup_surface::card(ctx->card_bg); }'),
  'Light popup handle line in the popup card\'s color');
assert.ok(!read('src/tiles/runtime/tile_renderer_shared.h').includes('switch_tile_card_color'), 'no tile card lookup for the popup');

// Web Admin preview rules.
const names = ['toneToLinear', 'toneToSrgb', 'toneOklch', 'toneHex', 'toneHaLightColor', 'toneLightAccent', 'toneLightShown',
  'panelThemeLight', 'panelLight'];
const js = vm.createContext({Math, Number, String, Array, parseInt, globalThis: {}});
vm.runInContext(names.map((name) => extractDeliveredFunction(name)).join('\n'), js);
const withTheme = (theme, run) => { js.document = {documentElement: {dataset: {panelTheme: theme}}}; return run(); };
const grid = read('src/web/admin/tiles/grid-preview.js');
assert.ok(grid.includes("(tileElem.dataset.followTint === '1' || !panelThemeLight()) && toneNoColor(given)"),
  'preview with_white: the dark theme fills white light too');
assert.ok(grid.includes("tileFillOf(tile?.icon_colors) > 0 && surfaceType !== '5'"), 'preview: a Switch tile has no idle grey');

// Device and preview: the same light color in both themes.
const host = await lvglHost(root);
if (!host) { console.log('SKIP: light white fill needs a C++ compiler'); process.exit(0); }
const out = path.join(root, 'build/tests/light-white-fill');
fs.mkdirSync(out, {recursive: true});
// 4000 K, 4392 K and 6500 K as Home Assistant reports them (#130), white,
// almost white, a saturated color.
const colors = [0xFFC9A6, 0xFFD6B7, 0xFFFDFA, 0xFFFFFF, 0xFFFEFA, 0xE53935];
const cpp = `#include <cstdio>
#include "src/ui/shared/tone_color.h"
int main() {
  const unsigned colors[] = {${colors.map((v) => v + 'u').join(', ')}};
  for (int theme = 0; theme < 2; ++theme) {
    ui_theme::set(theme);
    for (unsigned c : colors) std::printf("%d %u %u %u\\n", theme, c, tone_color::light_accent(c), tone_color::light_icon(c));
  }
  return 0;
}
`;
const file = path.join(out, 'test.cpp');
const binary = path.join(out, process.platform === 'win32' ? 'test.exe' : 'test');
fs.writeFileSync(file, cpp);
let result = spawnSync(host.cxx, ['-std=c++17', '-I', root, file, '-o', binary], {encoding: 'utf8'});
assert.equal(result.status, 0, result.stdout + result.stderr);
result = spawnSync(binary, [], {encoding: 'utf8'});
assert.equal(result.status, 0, result.stderr);
const rgb = (v) => [16, 8, 0].map((s) => (v >> s) & 255);
const near = (a, b) => rgb(a).every((v, i) => Math.abs(v - rgb(b)[i]) <= 1);
for (const line of result.stdout.trim().split(/\r?\n/)) {
  const [theme, color, accent, icon] = line.split(' ').map(Number);
  const name = `${theme ? 'light' : 'dark'} #${color.toString(16)}`;
  const ha = parseInt(js.toneHex(js.toneHaLightColor(rgb(color))).slice(1), 16);
  assert.ok(near(accent, ha) && near(icon, ha), `${name}: device light color is Home Assistant's`);
  withTheme(theme ? 'light' : 'dark', () => {
    assert.ok(near(parseInt(js.toneHex(js.toneLightAccent(rgb(color))).slice(1), 16), ha), `${name}: preview accent`);
    assert.ok(near(parseInt(js.toneLightShown('#' + color.toString(16).padStart(6, '0')).slice(1), 16), ha), `${name}: preview tile color`);
  });
}
// 4392 K fills like 4000 K: Home Assistant's lift leaves the "no color" range.
const toneOklch = (v) => js.toneOklch(rgb(v));
const lifted = parseInt(js.toneHex(js.toneHaLightColor(rgb(0xFFD6B7))).slice(1), 16);
assert.ok(toneOklch(lifted).L <= 0.9 || toneOklch(lifted).C >= 0.08, '4392 K lifted is a color, not white');
console.log('light white fill: HA colors in both themes, white fills, Switch tiles unfilled while off, popup like the plain tile');
