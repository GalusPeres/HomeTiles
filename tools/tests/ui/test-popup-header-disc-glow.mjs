// The popup header disc looks like the opening tile's disc: its Icon circle
// mode shows or hides it, and with "Circle in icon color" colored icons tint
// it (a color between icon and card) while white and grey icons keep the
// neutral white disc. Climate, Light and Cover keep the global background but
// take the tile's circle too. Popups without a tile tint by a colored icon.
// The card hairline stays the plain white border. The behavior runs in the
// LVGL host test (test-popup-header-value.mjs).
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n?/g, '\n');
const shell = read('src/ui/popups/popup_shell.cpp');

const tint = shell.slice(shell.indexOf('void apply_header_disc_tint('));
assert.ok(tint.startsWith('void apply_header_disc_tint('), 'Header tint helper exists');
for (const marker of [
  'const bool tinted = (r != g || g != b) && (!options.from_tile || options.glow);',
  '(!options.off && (!options.follows_global || ui_surface_style::icon_discs_shown()));',
  'const lv_color_t color = tinted ? lv_color_hex(rgb) : lv_color_white();',
  'ui_surface_style::apply_popup_border(shell.frame, lv_color_white(),',
  'static_cast<lv_opa_t>(popup_layout::kPopupBorderOpa));',
  'popup_layout::headerDiscContrastStep(',
  'tinted ? ui_surface_style::icon_glow_opa()',
  ': ui_surface_style::icon_neutral_opa(),',
  ': LV_OPA_TRANSP;',
]) assert.ok(tint.includes(marker), `header tint: ${marker}`);
// A newly shown popup takes the options its tile passed (or the default).
assert.match(shell, /if \(shell\.active != binding \|\| g_next_disc\.from_tile\) shell\.disc = g_next_disc;\s*g_next_disc = \{\};/);
// Every opener passes its tile's disc: popup_background() and, for Climate,
// Light and Cover (global background), forget_popup_source(card).
const source = read('src/tiles/runtime/tile_icon_source.cpp');
assert.match(source, /void forget_popup_source\(lv_obj_t\* obj\) \{\s*remember_popup_source\(nullptr\);\s*pass_popup_disc\(obj\);/);
assert.match(source, /uint32_t popup_background\(lv_obj_t\* obj, uint32_t fallback\) \{\s*remember_popup_source\(obj\);\s*pass_popup_disc\(obj\);/);
assert.ok(source.includes('popup_shell_use_tile_disc(mode == tile_icon_disc::Mode::Off, mode == tile_icon_disc::Mode::Global,'));
for (const [file, event] of [['src/types/climate/renderer.cpp', 'event'], ['src/types/cover/renderer.cpp', 'event'],
  ['src/types/switch/renderer.cpp', 'e']]) {
  assert.ok(read(file).includes(`tile_icon_source::forget_popup_source(static_cast<lv_obj_t*>(lv_event_get_current_target(${event})));`),
    `${file} passes its tile card`);
}
// The shell follows the copied icon color on every sync.
assert.match(shell, /copy_label\(shell\.icon, shell\.active->icon, false\);\s*apply_header_disc_tint\(shell\.icon_disc, shell\.icon\);/);
// Only a change touches the style (the sync runs every loop).
assert.match(tint, /if \(!lv_color_eq\(lv_obj_get_style_bg_color\(disc, LV_PART_MAIN\), color\)\)/);
assert.match(tint, /if \(lv_obj_get_style_bg_opa\(disc, LV_PART_MAIN\) != opa\)/);
// Same rule and opacities as the tile disc (popup code is compiled without it).
const tileDisc = read('src/tiles/runtime/tile_icon_disc.h');
assert.ok(tileDisc.includes('return r != g || g != b;'), 'Tiles use the same tint rule');
assert.doesNotMatch(tint, /icon_glow_border_opa/, 'The hairline never takes the icon hue');
assert.ok(tileDisc.includes('ui_surface_style::apply_icon_disc(disc, tinted, step, mode == Mode::Off, mode == Mode::Global);'));
assert.doesNotMatch(read('src/ui/popups/popup_layout.h'), /kHeaderIconDiscGlowOpa|kPopupBorderGlowOpa/);
assert.ok(read('src/ui/popups/popup_layout.h').includes('constexpr int kHeaderIconDiscOpa = 38;'));
// Dark cards get a subtler disc with the tile rule (8 % .. full in four steps).
const layoutH = read('src/ui/popups/popup_layout.h');
assert.ok(layoutH.includes('inline int headerDiscScaledOpa(int full, uint8_t step) { return (full * (24 + 7 * step) + 22) / 45; }'));
assert.ok(tileDisc.includes('return static_cast<lv_opa_t>((full * (24 + 7 * step) + 22) / 45);'));
for (const text of ['float t = (luma - 0.08f) / 0.17f;', 'return static_cast<uint8_t>(t * 3.0f + 0.5f);']) {
  assert.ok(layoutH.includes(text) && tileDisc.includes(text), `same contrast step: ${text}`);
}
// Sample colors: near-black steps down, colored tiles keep the full value.
const step = rgb => { const y = (0.2126 * (rgb >> 16 & 255) + 0.7152 * (rgb >> 8 & 255) + 0.0722 * (rgb & 255)) / 255; const t = Math.min(1, Math.max(0, (y - 0.08) / 0.17)); return Math.floor(t * 3 + 0.5); };
const opa = (full, s) => Math.floor((full * (24 + 7 * s) + 22) / 45);
assert.deepEqual([0x191C19, 0x222222, 0x282828, 0x1E3453, 0xA4530F].map(c => opa(38, step(c))), [20, 26, 26, 32, 38]);
assert.ok(read('src/tiles/runtime/tile_icon_disc.h').includes('inline constexpr lv_opa_t kOpa = icon_glow::kNeutralOpa;') &&
  read('src/core/config/icon_glow.h').includes('inline constexpr uint8_t kNeutralOpa = 38;'));
console.log('Popup header disc follows the tile circle options and the icon hue');
