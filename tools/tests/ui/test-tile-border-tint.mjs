// The tile border hairline stays mostly the tile, slightly lighter: white at
// about 20 %, or with a glowing icon a hint of its hue (the icon color halfway
// to white at the same 20 %). A red icon on a grey tile no longer gets a red
// border. Global border refreshes keep the hint; the popup hairline and the
// Web Admin preview follow the same rule, and a popup draws one hairline.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n?/g, '\n');

const header = read('src/ui/shared/ui_surface_style.h');
assert.ok(header.includes('return lv_color_mix(lv_color_white(), icon, 128);'), 'Border hint: icon halfway to white');
const style = read('src/ui/shared/ui_surface_style.cpp');
for (const marker of [
  'constexpr lv_opa_t kTileBorderOpa = 51;',
  'constexpr lv_style_selector_t kBorderTintStore = LV_PART_MAIN | LV_STATE_USER_4;',
  'lv_obj_get_local_style_prop(obj, LV_STYLE_OUTLINE_COLOR, &stored, kBorderTintStore)',
  'const lv_opa_t opa = kTileBorderOpa;',
  'void set_tile_border_tint(lv_obj_t* obj, lv_color_t icon) {',
  'store_border_color(host, border_hint(icon), true);',
  'void clear_tile_border_tint(lv_obj_t* obj) {',
  'lv_obj_remove_local_style_prop(host, LV_STYLE_OUTLINE_COLOR, kBorderTintStore);',
]) assert.ok(style.includes(marker), 'ui_surface_style: ' + marker);
assert.doesNotMatch(style + header, /icon_glow_border_opa|surface_hue|surface_accent/, 'No strong icon border, no tile-hue border');

// The hint is set when the tile is built; icon color changes never touch the
// border (a border change redraws the whole tile and any popup above it,
// which made a dragged Light color stutter).
const disc = read('src/tiles/runtime/tile_icon_disc.h');
const applyFill = disc.slice(disc.indexOf('inline void apply_fill(lv_obj_t* disc) {'), disc.indexOf('// A wrapped icon'));
assert.doesNotMatch(applyFill, /set_tile_border_tint|clear_tile_border_tint/, 'Icon color changes never touch the border');
assert.match(disc, /if \(glow && disc_mode != Mode::Off && icon_color_tints\(rgb\)\) \{\s*ui_surface_style::set_tile_border_tint\(card, lv_color_hex\(rgb\)\);\s*\} else \{\s*ui_surface_style::clear_tile_border_tint\(card\);/,
  'The border hint is set when the tile is built');

// The popup hairline is the plain white border and never follows the icon.
const shell = read('src/ui/popups/popup_shell.cpp');
assert.ok(shell.includes('ui_surface_style::apply_popup_border(shell.frame, lv_color_white(),'), 'Popup hairline is fixed');
assert.ok(!shell.includes('border_hint('), 'The popup hairline never follows the icon');
assert.ok(!shell.includes('ui_surface_style::apply_global_tile_border(parts.card);'), 'One popup hairline, not two');

// Preview: the same hint through --tile-border-tint at 20 %.
const preview = read('src/web/admin/tiles/grid-preview.js');
assert.ok(preview.includes("tileElem.style.setProperty('--tile-border-tint', 'rgba(' + hint.join(',') + ',0.20)');"));
assert.ok(preview.includes('const hint = iconRgb.map(v => Math.floor(((255 * 128 + v * 127) * 0x8081) / 0x800000));'));
const css = read('src/web/assets/admin.css');
assert.ok(css.includes('outline:1px solid var(--tile-border-tint, rgba(255,255,255,0.20));'));
assert.equal((css.match(/background:rgba\(255,255,255,var\(--icon-disc-opa, 0\.149\)\);/g) || []).length, 2, 'Neutral discs are white');
// LVGL mix == preview hint.
const mix = (c1, c2, m) => Math.floor(((c1 * m + c2 * (255 - m)) * 0x8081) / 0x800000);
for (const v of [0, 67, 128, 255]) {
  assert.equal(Math.floor(((255 * 128 + v * 127) * 0x8081) / 0x800000), mix(255, v, 128));
}
console.log('Tile borders stay mostly the tile with a hint of a glowing icon');
