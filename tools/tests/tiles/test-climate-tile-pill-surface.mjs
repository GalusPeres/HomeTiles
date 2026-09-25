// The Climate tile's control pill and its pressed +/- buttons are white
// overlays, a lighter step of whatever the tile shows (global color, own
// color or a rules tint), instead of a fixed grey that stood out on colored
// tiles. On the default 0x222222 tile they keep the former 0x3A3A3A pill and
// 0x4A4A4A pressed button. The Web Admin preview uses the same overlay.
import assert from 'node:assert/strict';
import {readRepoFile} from '../../lib/admin-source.mjs';

const read = file => readRepoFile(file).replace(/\r\n?/g, '\n');
const renderer = read('src/types/climate/renderer.cpp');
for (const marker of [
  'constexpr lv_opa_t kSlotSurfaceOpa = 28;',
  'constexpr lv_opa_t kSlotPressedOpa = 21;',
  'lv_obj_set_style_bg_color(root, lv_color_white(), LV_PART_MAIN);',
  'root, interactive_control ? kSlotSurfaceOpa : LV_OPA_TRANSP,',
  'button, kSlotPressedOpa,',
  ': kSlotPressedOpa,',
]) assert.ok(renderer.includes(marker), `climate renderer: ${marker}`);
assert.doesNotMatch(renderer, /0x3A3A3A\)|0x5A5A5A\)/, 'No fixed grey pill');

// Overlay math on the default tile (LVGL blends white at opa/255).
const over = (base, opa) => Math.round(base + (255 - base) * opa / 255);
assert.equal(over(0x22, 28), 0x3A, 'Pill on the default tile');
assert.equal(over(over(0x22, 28), 21), 0x4A, 'Pressed button on the pill');

const css = read('src/web/assets/admin.css');
assert.match(css, /\.tile\.climate \.climate-slot-control \{[^}]*background:rgba\(255,255,255,0\.11\);/,
  'Preview pill is the same overlay');
console.log('Climate tile pill follows the tile color');
