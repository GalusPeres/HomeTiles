// The tile border hairline is always neutral: white at about 20 %, a slightly
// lighter step of whatever color the tile shows (global, own color or a rules
// tint). It never takes the icon hue, so a red icon on a grey tile keeps a
// grey border. Global border refreshes and the Web Admin preview match.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n?/g, '\n');

const style = read('src/ui/shared/ui_surface_style.cpp');
for (const marker of [
  'constexpr lv_opa_t kTileBorderOpa = 51;',
  'const lv_color_t color = lv_color_white();',
  'const lv_opa_t opa = kTileBorderOpa;',
  'lv_obj_set_style_outline_color(obj, color, selector);',
  'lv_obj_set_style_outline_opa(obj, enabled ? opa : LV_OPA_TRANSP, selector);',
]) assert.ok(style.includes(marker), `ui_surface_style: ${marker}`);
for (const source of [style, read('src/ui/shared/ui_surface_style.h'), read('src/tiles/runtime/tile_icon_disc.h')]) {
  assert.doesNotMatch(source, /set_tile_border_tint|clear_tile_border_tint|kBorderTintStore/, 'No icon hue border tint');
}

// Preview: the same neutral outline, no per-tile tint variable.
const preview = read('src/web/admin/tiles/grid-preview.js');
assert.ok(!preview.includes('--tile-border-tint'), 'Preview sets no border tint');
const css = read('src/web/assets/admin.css');
assert.ok(css.includes('outline:1px solid rgba(255,255,255,0.20);'));
assert.ok(!css.includes('--tile-border-tint'));
console.log('Tile borders stay the neutral hairline');
