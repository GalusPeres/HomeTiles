// Half-step tiles must get their pixel geometry before a renderer runs
// lv_obj_update_layout(): LVGL keeps a grid-stretched size and ignores a later
// lv_obj_set_size(), which left a 3.5-wide Weather card at 3 cells.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n/g, '\n');

const shared = read('src/tiles/runtime/tile_renderer_shared.h');
const helper = shared.slice(shared.indexOf('inline void place_tile_card('));
assert.ok(helper.length > 0, 'place_tile_card helper exists');
assert.ok(helper.indexOf('set_tile_grid_cell(') < helper.indexOf('apply_fractional_tile_geometry('),
  'place_tile_card applies half-step geometry right after the grid cell');

const typesDir = path.join(root, 'src/types');
for (const type of fs.readdirSync(typesDir)) {
  const file = path.join('src/types', type, 'renderer.cpp');
  if (!fs.existsSync(path.join(root, file))) continue;
  const source = read(file);
  assert.ok(!/set_tile_grid_cell\([^;]*tile\.span_w/.test(source),
    `${file} places its card with place_tile_card`);
}
console.log('Fractional tile placement: PASS');
