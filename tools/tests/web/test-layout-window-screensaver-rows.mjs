// The layout window and the screensaver's rows (user 2026-10-28, extended
// 2026-11-04: switching between upright and landscape failed with 409 until a
// screensaver tile was moved by hand). The screensaver now uses the whole
// screen (screensaver_places::first_row returns 0): every row of a layout
// takes a screensaver tile, no place is refused for being above the old
// bottom rows, and a take-over keeps every tile that fits the screen. A place
// half past the screen's edge is still red, so nothing is sent that the panel
// refuses.
import assert from 'node:assert/strict';
import vm from 'node:vm';
import {extractDeliveredFunction} from '../../lib/admin-source.mjs';

const names = ['layoutClone', 'layoutEmptyTile', 'layoutUsed', 'layoutShown', 'layoutNavType', 'layoutBarHidden',
  'layoutBarTile', 'layoutCanvas', 'layoutInside', 'layoutFolderId', 'classicHidden', 'classicParked', 'layoutParked',
  'layoutBase', 'layoutSettle', 'takeoverTiles', 'setupTiles', 'isCompactSensorType', 'isEditableValueType',
  'supportsHalfSize', 'supportedTileLayout', 'normalizeLayoutForTileType', 'clampHalf', 'headBarLayout', 'placeCols',
  'placeRows', 'layoutMustReach', 'layoutRed', 'layoutParkSettings', 'layoutSettingsIndex', 'layoutSettingsIn',
  'layoutSettingsOut', 'screensaverFirstRow'];

const SAVER_FOLDER = 65534;
const tile = (type, view) => ({type, col: 0, row: 0, span_w: 1, span_h: 1, view_id: view});
// Three screensaver tiles (their slot keys as IDs); classic places in the
// classic screen's two bottom rows, upright ones in its three bottom rows,
// with the Guition V2's grids (classic 7 x 5, bar 6 x 4, upright 4 x 6.5).
const saver = [tile(2, 1), tile(2, 2), tile(2, 3), {type: 0}];
const classic = {[SAVER_FOLDER]: [[0, 1, 0, 3, 1, 1], [1, 2, 1, 4, 1, 1], [2, 3, 2, 4, 1, 1]]};
const places = {pages: [], portrait: {[SAVER_FOLDER]: {1: [0, 3.5, 1, 1], 2: [1, 4.5, 1, 1], 3: [2, 5.5, 1, 1]}}};

const ctx = vm.createContext({
  LAYOUT_KEYS: ['classic', 'bar', 'portrait'],
  LAYOUTS: {
    classic: {name: 'Klassisch', cols: 7, rows: 5, bar: false, available: true, switchable: true},
    bar: {name: 'Kopfleiste quer', cols: 6, rows: 4, bar: true, available: true, switchable: true},
    portrait: {name: 'Kopfleiste hochkant', cols: 4, rows: 6.5, bar: true, portrait: true, available: true, switchable: true},
  },
  // The window's grid (useLayoutGrid): the canvas around every layout.
  ACTIVE_LAYOUT: 'portrait', MEDIA_TILE_TYPE: 15, MEDIA_TILE_MIN_SPAN: 2, MEDIA_TILE_MAX_SPAN: 4, GRID_COLS: 7, GRID_ROWS: 7,
  GRID_SHOWN_COLS: 7, GRID_SHOWN_ROWS: 7, HEAD_BAR: false, currentTileTab: 'folder0', useLayoutGrid: () => {},
  getFolderIdForTab: tab => tab === 'screensaver' ? SAVER_FOLDER : Number(tab.slice('folder'.length)),
  isScreensaverTileTab: tab => tab === 'screensaver',
});
vm.runInContext(names.map(extractDeliveredFunction).join('\n'), ctx);
ctx.layoutWindow = {pages: {screensaver: saver}, data: {success: true, classic, places}, key: 'bar', work: {}, saved: {},
  settingsParked: {}, savedSettingsParked: {}};
const run = code => vm.runInContext(code, ctx);
const placeOf = (list, view) => {
  const t = list.find(entry => entry && entry.view_id === view);
  return [t.col, t.row, t.span_w, t.span_h];
};

// No rows are reserved (screensaver_places::first_row): the screensaver uses
// the whole screen of every layout.
assert.equal(run("screensaverFirstRow('bar', 4)"), 0, 'landscape: no reserved rows');
assert.equal(run("screensaverFirstRow('portrait', 6.5)"), 0, 'upright: no reserved rows');
assert.equal(run("screensaverFirstRow('classic', 5)"), 0, 'classic: no reserved rows');

// Every row of the screen takes a screensaver tile; only a place half past
// the edge is red (a folder must stay reachable).
const at = (col, row) => JSON.stringify({type: 2, col, row, span_w: 1, span_h: 1});
assert.equal(run(`layoutInside(${at(0, 1)}, 'bar', 'screensaver')`), true, 'an upper row is on the screen');
assert.equal(run(`layoutRed(${at(0, 1)}, 'bar', 'screensaver')`), false, 'no row of the screen is red');
assert.equal(run(`layoutRed(${at(0, 3)}, 'portrait', 'screensaver')`), false, 'upright rows are free too');
const edge = JSON.stringify({type: 4, col: 6.5, row: 0, span_w: 1, span_h: 1});
assert.equal(run(`layoutRed(${edge}, 'bar', 'screensaver')`), true, 'past the right edge is red');
assert.equal(run(`layoutRed(${at(0, 1)}, 'bar', 'folder0')`), false, 'a folder uses the whole screen');
assert.equal(run(`layoutInside(${at(0, 1)}, 'bar')`), true, 'without a tab: the whole screen');

// "Kopieren von" upright into landscape: every upright row keeps its place on
// the landscape screen (the top one was parked only while the bottom rows
// were reserved).
{
  const tiles = run("takeoverTiles('screensaver', 'bar', 'portrait')");
  assert.deepEqual(placeOf(tiles, 1), [0, 1, 1, 1], 'the top upright row fits the landscape screen');
  assert.deepEqual(placeOf(tiles, 2), [1, 2, 1, 1]);
  assert.deepEqual(placeOf(tiles, 3), [2, 3, 1, 1]);
  const top = tiles.find(entry => entry.view_id === 1);
  assert.equal(ctx.layoutParked(top, 'bar'), false, 'no upright row goes to the storage');
  for (const entry of tiles.filter(item => item.type)) {
    assert.equal(ctx.layoutRed(entry, 'bar', 'screensaver'), false, `tile ${entry.view_id} is not red`);
    assert.ok(ctx.layoutInside(entry, 'bar', 'screensaver'), `tile ${entry.view_id} is on the screen`);
  }
}

// The classic layout from upright as well: every upright row keeps its place.
{
  const tiles = run("takeoverTiles('screensaver', 'classic', 'portrait')");
  assert.deepEqual(placeOf(tiles, 1), [0, 2, 1, 1]);
  assert.deepEqual(placeOf(tiles, 2), [1, 3, 1, 1]);
  assert.deepEqual(placeOf(tiles, 3), [2, 4, 1, 1]);
  assert.equal(ctx.layoutParked(tiles.find(entry => entry.view_id === 1), 'classic'), false);
}

// From classic into upright everything fits (two rows into three).
{
  const tiles = run("takeoverTiles('screensaver', 'portrait', 'classic')");
  assert.deepEqual(placeOf(tiles, 1), [0, 4.5, 1, 1]);
  assert.deepEqual(placeOf(tiles, 2), [1, 5.5, 1, 1]);
  assert.deepEqual(placeOf(tiles, 3), [2, 5.5, 1, 1]);
}
console.log('Layout window: screensaver tiles use the whole screen of every layout');
