// The Climate tile's control pill and its pressed +/- buttons follow the
// popup control rule (tile_icon_source::refresh_controls): the circle's color
// (tone_color::fill) at the control opacity, instead of a fixed white overlay
// that ignored the icon. The Web Admin preview uses the same rule.
import assert from 'node:assert/strict';
import {readRepoFile} from '../../lib/admin-source.mjs';

const read = file => readRepoFile(file).replace(/\r\n?/g, '\n');
const renderer = read('src/types/climate/renderer.cpp');
for (const marker of [
  // The pill is a resting surface, - and + are press fills.
  'tile_icon_disc::mark_surface(root);',
  'tile_icon_disc::mark_control(button);',
  // The shared fill shows while the slot is interactive; compact layouts press
  // without a fill; the rule is applied once the card is built.
  'lv_obj_remove_local_style_prop(root, LV_STYLE_BG_OPA, LV_PART_MAIN);',
  'lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_PRESSED);',
  'lv_obj_remove_local_style_prop(button, LV_STYLE_BG_OPA, LV_PART_MAIN | LV_STATE_PRESSED);',
  'tile_icon_source::refresh_controls(card);',
]) assert.ok(renderer.includes(marker), `climate renderer: ${marker}`);
assert.doesNotMatch(renderer, /0x3A3A3A\)|0x5A5A5A\)|kSlotSurfaceOpa|kSlotPressedOpa/, 'No fixed pill overlay');

// refresh_controls styles controls on the card and one level deeper.
const source = read('src/tiles/runtime/tile_icon_source.cpp');
assert.match(source, /const bool press = tile_icon_disc::is_control\(obj\);\s*if \(!press && !tile_icon_disc::is_surface\(obj\)\) return;/);
assert.match(source, /for \(uint32_t j = 0; j < inner; \+\+j\) style\(lv_obj_get_child\(child, static_cast<int32_t>\(j\)\)\);/);

// Preview: the pill takes --control-fill, set per tile with the same rule.
const css = read('src/web/assets/admin.css');
assert.match(css, /\.tile\.climate \.climate-slot-control \{[^}]*background:var\(--control-fill, rgba\(255,255,255,0\.094\)\);/,
  'Preview pill uses the control fill');
const preview = read('src/web/admin/tiles/grid-preview.js');
assert.ok(preview.includes("tileElem.style.setProperty('--control-fill', rgba(tone.controlOpa));"),
  'Preview pill takes the circle color at the control opacity');
console.log('Climate tile pill follows the popup control rule');
