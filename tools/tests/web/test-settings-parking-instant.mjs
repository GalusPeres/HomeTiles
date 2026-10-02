// Moving the Settings tile into or out of the Home parking slot shows at once
// (user 2026-10-02: the tile jumped back and took long to move). The preview
// draws the move before the device saves it, leaves the grid data to the
// device's reload, a failed save draws the stored state back, and parking
// saves the grid tile first only when it has a pending edit.
import assert from 'node:assert/strict';
import vm from 'node:vm';
import {extractDeliveredFunction} from '../../lib/admin-source.mjs';

const preview = extractDeliveredFunction('previewSettingsTileTransfer');
function run(tiles, hidden, target) {
  const calls = [];
  const context = {
    tiles,
    sensorMetaCache: {},
    getTilesData: () => tiles,
    renderTileFromData: (tab, index, tile) => calls.push(['tile', tab, index, {...tile}]),
    layoutTiles: (tab, list) => calls.push(['layout', tab, list.map(tile => Number(tile?.type || 0))]),
    renderSettingsHiddenSlot: (isHidden, snapshot) => calls.push(['slot', isHidden, snapshot.title]),
    hidden,
    target,
    snapshot: {title: 'Settings', icon: 'cog', bg_color: 0, span_w: '1', span_h: '1', col: '6', row: '1'}
  };
  vm.runInNewContext(`${preview}; previewSettingsTileTransfer(hidden, snapshot, target)`, context);
  return calls;
}

// Parking: the grid cell empties and the slot shows the tile, at once.
const grid = [{type: 4}, {type: 7, col: 5, row: 0}, {type: 0}, {type: 0}];
const before = JSON.stringify(grid);
let calls = run(grid, true, null);
assert.deepEqual(calls[0], ['tile', 'folder0', 1, {type: 0}]);
assert.deepEqual(calls[1], ['layout', 'folder0', [4, 0, 0, 0]]);
assert.deepEqual(calls[2], ['slot', true, 'Settings']);
assert.equal(JSON.stringify(grid), before, 'the grid data stays the device\'s');

// Restoring: the first empty index (TileConfig::ensureSettingsTile) takes the
// tile at the drop target, and the slot empties.
const parked = [{type: 4}, {type: 0}, {type: 0}];
calls = run(parked, false, {col: 2, row: 1.5});
assert.equal(calls[0][2], 1);
assert.deepEqual({...calls[0][3]}, {type: 7, title: 'Settings', icon_name: 'cog', bg_color: 0,
  col: 2, row: 1.5, span_w: '1', span_h: '1'});
assert.deepEqual(calls[1], ['layout', 'folder0', [4, 7, 0]]);
assert.deepEqual(calls[2], ['slot', false, 'Settings']);
assert.deepEqual(parked.map(tile => tile.type), [4, 0, 0]);
assert.equal(run([{type: 7}, {type: 0}], false, {col: 0, row: 0}).length, 0, 'never a second Settings tile');

// The moves call it before any save, and draw the device state back on failure.
const hide = extractDeliveredFunction('hideSettingsTileFromGrid');
const restore = extractDeliveredFunction('restoreHiddenSettingsTile');
const at = (source, text) => {
  const index = source.indexOf(text);
  assert.ok(index >= 0, text);
  return index;
};
assert.ok(at(hide, 'previewSettingsTileTransfer(true,') < at(hide, 'flushSettingsTileSaveBeforeHide('));
assert.ok(at(restore, 'previewSettingsTileTransfer(false,') < at(restore, 'queueSettingsAccessSave('));
assert.ok(hide.includes('if(!saved){await reconcileSettingsTileUi(false);return false}'));
assert.ok(restore.includes('if(!saved){await reconcileSettingsTileUi(true,snapshot);return false}'));
assert.equal((hide + restore).split('flushDeferredSensorRefresh()').length - 1, 2);

// No extra tile save without a pending edit: the parking save carries the
// snapshot, and a second save made the device write and rebuild twice.
const flush = extractDeliveredFunction('flushSettingsTileSaveBeforeHide');
assert.ok(flush.includes('const pending=!!autoSaveTimers[timerKey]||!!drafts?.[tab]?.[index]?._dirty;') &&
  flush.includes('if(pending)saveTile(tab,true,index);'));

// The 15 s value refresh waits while a move is on its way.
const load = extractDeliveredFunction('loadSensorValues');
assert.equal(load.split('dragSource||resizeState||settingsTileTransferInFlight').length - 1, 2);

console.log('Settings parking: moves show at once, the device state follows');
