// Moving the Settings tile into or out of the Home parking slot shows at once
// (user 2026-10-02: the tile jumped back, its teal selection lagged and it
// took long to move). The preview moves the tile, the grid data and the
// selection before the device saves; moves queue instead of being dropped and
// only the latest one reconciles; parking sends no extra tile save. The full
// flow runs in test-settings-parking-roundtrip.mjs.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {fileURLToPath} from 'node:url';
import {extractDeliveredFunction} from '../../lib/admin-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');

const preview = extractDeliveredFunction('previewSettingsTileTransfer');
function run(tiles, hidden, target) {
  const calls = [];
  const context = {
    sensorMetaCache: {},
    getTilesData: () => tiles,
    renderTileFromData: (tab, index, tile) => calls.push(['tile', tab, index, {...tile}]),
    layoutTiles: (tab, list) => calls.push(['layout', tab, list.map(tile => Number(tile?.type || 0))]),
    renderSettingsHiddenSlot: (isHidden, snapshot) => calls.push(['slot', isHidden, snapshot.title]),
    selectHiddenSettingsTile: () => calls.push(['select', 'parked']),
    selectTile: (index, tab) => calls.push(['select', tab, index]),
    hidden,
    target,
    snapshot: {title: 'Settings', icon: 'cog', bg_color: 0, span_w: '1', span_h: '1', col: '6', row: '1'}
  };
  vm.runInNewContext(`${preview}; previewSettingsTileTransfer(hidden, snapshot, target)`, context);
  return calls;
}

// Parking: the grid cell and its data empty, the slot shows the tile and
// takes the selection, at once.
const grid = [{type: 4}, {type: 7, col: 5, row: 0}, {type: 0}, {type: 0}];
let calls = run(grid, true, null);
assert.deepEqual(calls, [['tile', 'folder0', 1, {type: 0}], ['layout', 'folder0', [4, 0, 0, 0]],
  ['slot', true, 'Settings'], ['select', 'parked']]);
assert.deepEqual(grid.map(tile => tile.type), [4, 0, 0, 0], 'the grid data follows the move');

// Restoring: the first empty index (TileConfig::ensureSettingsTile) takes the
// tile at the drop target and the selection, and the slot empties.
const parked = [{type: 4}, {type: 0}, {type: 0}];
calls = run(parked, false, {col: 2, row: 1.5});
assert.deepEqual(calls[0], ['tile', 'folder0', 1, {type: 7, title: 'Settings', icon_name: 'cog', bg_color: 0,
  col: 2, row: 1.5, span_w: '1', span_h: '1'}]);
assert.deepEqual(calls.slice(1), [['layout', 'folder0', [4, 7, 0]], ['slot', false, 'Settings'],
  ['select', 'folder0', 1]]);
assert.deepEqual(parked.map(tile => tile.type), [4, 7, 0]);
assert.equal(run([{type: 7}, {type: 0}], false, {col: 0, row: 0}).length, 0, 'never a second Settings tile');

// Moves queue: no in-flight refusal, each shows before its save, only the
// latest reconciles, a failed save draws the stored state back.
const hide = extractDeliveredFunction('hideSettingsTileFromGrid');
const restore = extractDeliveredFunction('restoreHiddenSettingsTile');
const at = (source, text) => {
  const index = source.indexOf(text);
  assert.ok(index >= 0, text);
  return index;
};
for (const source of [hide, restore]) {
  assert.ok(!source.includes('settingsTileTransferInFlight') && source.includes('settingsTileTransfersInFlight++'));
  assert.ok(source.includes('const transfer=++settingsTileTransferSeq;') &&
    source.includes('if(transfer!==settingsTileTransferSeq)return saved;'));
  assert.equal(source.split('flushDeferredSensorRefresh()').length - 1, 1);
}
assert.ok(at(hide, 'previewSettingsTileTransfer(true,') < at(hide, 'flushSettingsTileSaveBeforeHide('));
assert.ok(at(restore, 'previewSettingsTileTransfer(false,') < at(restore, 'queueSettingsAccessSave('));
assert.ok(hide.includes('if(!saved){await reconcileSettingsTileUi(false);return false}'));
assert.ok(restore.includes('if(!saved){await reconcileSettingsTileUi(true,snapshot);return false}'));

// No extra tile save before parking: the parking save carries the snapshot,
// and a second save made the device write and rebuild twice.
const flush = extractDeliveredFunction('flushSettingsTileSaveBeforeHide');
assert.ok(flush.includes('clearDraft(tab,index);') && !flush.includes('saveTile('));

// The 15 s value refresh waits while a move is on its way.
const load = extractDeliveredFunction('loadSensorValues');
assert.equal(load.split('dragSource||resizeState||settingsTileTransfersInFlight').length - 1, 2);

// The panel shows a parked or restored Settings tile before the flash write,
// like a tile reorder, and then rebuilds only the Home grid's hidden caches
// instead of every grid.
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n?/g, '\n');
const tileConfig = read('src/tiles/config/tile_config.cpp');
const visible = tileConfig.slice(tileConfig.indexOf('SettingsTileVisibilityResult TileConfig::setSettingsTileVisible('));
assert.ok(visible.indexOf('if (changed && show) show(grid);') >= 0 &&
  visible.indexOf('if (changed && show) show(grid);') < visible.indexOf('saveGridInPlace(kRootFolderId, grid, false)'));
const handlers = read('src/web/server/handlers/web_admin_handlers.cpp');
assert.ok(handlers.includes('tileConfig.previewActiveFolderGrid(kHomeFolderId, grid) &&\n      tiles_show_active_layout_now();'));
assert.ok(handlers.includes('settings_tile_target_row, show_settings_tile_now);'));
const after = handlers.slice(handlers.indexOf('} else if (settings_visibility_commit_needed) {'));
assert.ok(after.startsWith('} else if (settings_visibility_commit_needed) {') &&
  after.slice(0, 500).includes('tiles_invalidate_folder_only(kHomeFolderId);') &&
  after.slice(0, 500).includes('if (!g_settings_tile_shown_now &&') &&
  !after.slice(0, 500).includes('tiles_request_reload_all();'));

console.log('Settings parking: moves show at once, queue, and the device state follows');
