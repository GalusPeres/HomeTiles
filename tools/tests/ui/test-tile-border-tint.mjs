// The tile border hairline is the tile's own hue at about 20 %: a slightly
// lighter step of whatever color the tile shows (global, own color or a rules
// tint), white on grey tiles. It never takes the icon hue, so a red icon on a
// grey tile keeps a grey border and a green tile gets a lighter green border.
// A rules tint refreshes the border; popups draw a single hairline the same
// way. The Web Admin preview matches.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n?/g, '\n');

const style = read('src/ui/shared/ui_surface_style.cpp');
for (const marker of [
  'constexpr lv_opa_t kTileBorderOpa = 51;',
  '? surface_hue(lv_obj_get_style_bg_color(obj, LV_PART_MAIN))',
  'lv_obj_set_style_outline_color(obj, color, selector);',
  'lv_obj_set_style_outline_opa(obj, enabled ? opa : LV_OPA_TRANSP, selector);',
  'void refresh_tile_border(lv_obj_t* obj) {',
]) assert.ok(style.includes(marker), `ui_surface_style: ${marker}`);
for (const source of [style, read('src/ui/shared/ui_surface_style.h'), read('src/tiles/runtime/tile_icon_disc.h')]) {
  assert.doesNotMatch(source, /set_tile_border_tint|clear_tile_border_tint|kBorderTintStore/, 'No icon hue border tint');
}
assert.ok(read('src/tiles/runtime/tile_icon_source.cpp').includes('ui_surface_style::refresh_tile_border(card);'),
  'A rules tint refreshes the border');
const shell = read('src/ui/popups/popup_shell.cpp');
assert.ok(shell.includes('ui_surface_style::apply_popup_border(shell.frame, card_hue,'));
assert.ok(!shell.includes('ui_surface_style::apply_global_tile_border(parts.card);'), 'One popup hairline, not two');

// Preview: the same hue on the outline through --tile-hue-rgb.
const preview = read('src/web/admin/tiles/grid-preview.js');
assert.ok(preview.includes("tileElem.style.setProperty('--tile-hue-rgb', tileSurfaceHue(bg).join(','));"));
const css = read('src/web/assets/admin.css');
assert.ok(css.includes('outline:1px solid rgba(var(--tile-hue-rgb, 255,255,255),0.20);'));
assert.equal((css.match(/background:rgba\(var\(--tile-hue-rgb, 255,255,255\),var\(--icon-disc-opa, 0\.149\)\);/g) || []).length, 2,
  'Neutral discs take the tile hue');
console.log('Tile and popup borders take the tile hue');
