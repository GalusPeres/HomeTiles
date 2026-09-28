// A new tile placed where a tile had been deleted took over the deleted
// tile's entity. Deletion only changed type, title, icon and color: the
// firmware packed the old entity and options with the empty tile and served
// them again, and the editor merged the empty snapshot over the old data.
// Empty tiles now keep only their slot geometry on both sides.
import assert from 'node:assert/strict';

import {extractDeliveredFunction, readRepoFile} from '../../lib/admin-source.mjs';
import {maskCpp} from '../../lib/cpp-source.mjs';

// ---- Editor: the real delivered function -----------------------------------
const tilesData = {};
const applySnapshotToTileData = new Function('tilesData', `
  const getTilesData = tab => tilesData[tab];
  const normalizeSnapshotLayout = snapshot => ({
    col: Number(snapshot.col) || 0, row: Number(snapshot.row) || 0,
    span_w: Number(snapshot.span_w) || 1, span_h: Number(snapshot.span_h) || 1});
  const clampInt = (value, min, max, fallback) => {
    const number = Number(value);
    return Number.isFinite(number) ? Math.min(max, Math.max(min, Math.round(number))) : fallback;
  };
  const snapshotBgColorIsDefault = () => true;
  const makeTileBgValue = value => value;
  const hexToRgb = () => 0;
  ${extractDeliveredFunction('applySnapshotToTileData')}
  return applySnapshotToTileData;`)(tilesData);

tilesData.home = [];
tilesData.home[3] = {type: 1, title: 'Old', sensor_entity: 'sensor.old', sensor_unit: 'W',
                     sensor_decimals: 2, popup_open_mode: 1, col: 2, row: 1, span_w: 1, span_h: 1};
applySnapshotToTileData('home', 3, {type: '0', title: '', icon: '', col: 2, row: 1, span_w: 1, span_h: 1});
const deleted = tilesData.home[3];
assert.equal(deleted.type, 0);
for (const field of ['sensor_entity', 'sensor_unit', 'sensor_decimals', 'popup_open_mode']) {
  assert.equal(deleted[field], undefined, `a deleted tile drops ${field}`);
}
assert.deepEqual([deleted.col, deleted.row, deleted.span_w, deleted.span_h], [2, 1, 1, 1],
  'the slot keeps its geometry');

applySnapshotToTileData('home', 3, {type: '1', title: 'New', sensor_entity: 'sensor.new',
                                    col: 2, row: 1, span_w: 1, span_h: 1});
assert.equal(tilesData.home[3].sensor_entity, 'sensor.new', 'a new tile takes only its own entity');

// Other edits still merge with the stored data.
applySnapshotToTileData('home', 3, {type: '1', title: 'Renamed', col: 2, row: 1, span_w: 1, span_h: 1});
assert.equal(tilesData.home[3].sensor_entity, 'sensor.new');

// ---- Firmware: deletion, save and load --------------------------------------
const header = maskCpp(readRepoFile('src/tiles/config/tile_config.h'));
assert.match(header,
  /static inline void clearEmptyTileFields\(Tile& tile\) \{\s*Tile empty;\s*empty\.col = tile\.col;\s*empty\.row = tile\.row;\s*empty\.span_w = tile\.span_w;\s*empty\.span_h = tile\.span_h;\s*tile = empty;\s*\}/,
  'an empty tile keeps only its slot geometry');

const handler = maskCpp(readRepoFile('src/web/server/handlers/web_admin_tiles.cpp'));
const cleared = handler.indexOf('if (tile.type == TILE_EMPTY) clearEmptyTileFields(tile);');
assert.ok(cleared > 0, 'deletion clears the tile');
assert.ok(cleared < handler.indexOf('tileConfig.saveFolderGrid(folder_id, *grid)'),
  'before the grid is saved');

const config = maskCpp(readRepoFile('src/tiles/config/tile_config.cpp'));
assert.match(config, /grid\.tiles\[i\] = Tile\{\};\s*changed = true;\s*\} else if \(grid\.tiles\[i\]\.type == TILE_EMPTY\) \{\s*clearEmptyTileFields\(grid\.tiles\[i\]\);/,
  'loading drops fields that older firmware stored with empty tiles');
assert.match(config, /working\.tiles\[i\] = Tile\{\};\s*\} else if \(working\.tiles\[i\]\.type == TILE_EMPTY\) \{\s*clearEmptyTileFields\(working\.tiles\[i\]\);/,
  'saving never stores fields with an empty tile');

console.log('Deleted tiles keep only their slot; a new tile starts without the old entity');
