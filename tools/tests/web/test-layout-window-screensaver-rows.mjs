// The layout window and the screensaver's rows (user 2026-10-28: switching
// between upright and landscape failed with 409 until a screensaver tile was
// moved by hand). The panel takes screensaver tiles only in the rows at the
// bottom of a layout's screen, three upright and two otherwise, and refuses
// a place above them (/api/layouts "Invalid place"). The window showed such
// a place green: a take-over from the upright layout moved its top
// screensaver row one row above the landscape screensaver rows. Now a tile
// above them goes to the storage like the panel leaves it out, and one that
// lies there is red, so nothing is sent that the panel refuses.
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

// The rows the panel takes (screensaver_places::first_row).
assert.equal(run("screensaverFirstRow('bar', 4)"), 2, 'landscape: the two bottom rows');
assert.equal(run("screensaverFirstRow('portrait', 6.5)"), 3.5, 'upright: the three bottom rows');
assert.equal(run("screensaverFirstRow('classic', 5)"), 3);

// A screensaver tile above its rows is red, a folder's tile there is not.
const at = (col, row) => JSON.stringify({type: 2, col, row, span_w: 1, span_h: 1});
assert.equal(run(`layoutRed(${at(0, 1)}, 'bar', 'screensaver')`), true, 'above the landscape screensaver rows');
assert.equal(run(`layoutRed(${at(0, 2)}, 'bar', 'screensaver')`), false);
assert.equal(run(`layoutRed(${at(0, 3)}, 'portrait', 'screensaver')`), true, 'above the upright screensaver rows');
assert.equal(run(`layoutRed(${at(0, 3.5)}, 'portrait', 'screensaver')`), false);
assert.equal(run(`layoutRed(${at(0, 1)}, 'bar', 'folder0')`), false, 'a folder uses the whole screen');
assert.equal(run(`layoutInside(${at(0, 1)}, 'bar')`), true, 'without a tab: the whole screen');

// "Kopieren von" upright into landscape: the two lower rows keep their place
// on the screen's bottom, the top one goes to the storage (it was row 1, the
// place the panel refused).
{
  const tiles = run("takeoverTiles('screensaver', 'bar', 'portrait')");
  assert.deepEqual(placeOf(tiles, 2), [1, 2, 1, 1]);
  assert.deepEqual(placeOf(tiles, 3), [2, 3, 1, 1]);
  const top = tiles.find(entry => entry.view_id === 1);
  assert.equal(ctx.layoutParked(top, 'bar'), true, 'the top upright row goes to the storage');
  for (const entry of tiles.filter(item => item.type)) {
    assert.equal(ctx.layoutRed(entry, 'bar', 'screensaver'), false, `tile ${entry.view_id} is not red`);
    assert.ok(!ctx.layoutInside(entry, 'bar', 'screensaver') || entry.row >= 2, `tile ${entry.view_id} in the rows sent`);
  }
}

// The classic layout from upright as well: its two bottom rows.
{
  const tiles = run("takeoverTiles('screensaver', 'classic', 'portrait')");
  assert.deepEqual(placeOf(tiles, 2), [1, 3, 1, 1]);
  assert.deepEqual(placeOf(tiles, 3), [2, 4, 1, 1]);
  assert.equal(ctx.layoutParked(tiles.find(entry => entry.view_id === 1), 'classic'), true);
}

// From classic into upright everything fits (two rows into three).
{
  const tiles = run("takeoverTiles('screensaver', 'portrait', 'classic')");
  assert.deepEqual(placeOf(tiles, 1), [0, 4.5, 1, 1]);
  assert.deepEqual(placeOf(tiles, 2), [1, 5.5, 1, 1]);
  assert.deepEqual(placeOf(tiles, 3), [2, 5.5, 1, 1]);
}
console.log('Layout window: screensaver tiles keep to the rows the panel takes');
