// The layouts (grid_layout.h, tile_layouts.h) in the Web Admin: the editor
// places and moves tiles inside the active layout's grid and leaves out the
// tiles without a place there; the layout window ("Layout ändern") lays a
// layout out in one area for all three, takes another one over (folders
// become pills in the bar's two half rows), keeps tiles without a place
// beside the screen (red), and stores places by stable tile ID. The server
// checks the places, keeps the classic ones in the tile data and the others
// in the layout file, and the panel starts with the chosen layout.
import assert from 'node:assert/strict';
import vm from 'node:vm';
import {extractDeliveredFunction, readRepoFile} from '../../lib/admin-source.mjs';

// --- The editor with a bar layout: the shown grid, hidden tiles take no room.
{
  const names = ['headBarLayout', 'placeCols', 'placeRows', 'tileTakesRoom', 'clampHalf', 'markOccupied',
    'slotFits', 'firstFreeSlot'];
  const ctx = vm.createContext({
    HEAD_BAR: true, GRID_COLS: 7, GRID_ROWS: 5, GRID_SHOWN_COLS: 6, GRID_SHOWN_ROWS: 4,
    currentTileTab: 'folder0', isScreensaverTileTab: tab => tab === 'screensaver',
    firstAllowedGridRow: tab => (tab === 'screensaver' ? 3 : 0),
  });
  vm.runInContext(names.map(extractDeliveredFunction).join('\n'), ctx);
  const run = code => vm.runInContext(code, ctx);
  assert.equal(run("placeCols('folder0')"), 6);
  assert.equal(run("placeRows('folder3')"), 4);
  assert.equal(run("placeCols('screensaver')"), 7, 'the screensaver keeps its stored grid');
  run('var occupied = Array.from({length: 10}, () => Array(14).fill(false));');
  assert.equal(run("slotFits('folder0', occupied, 6, 0, 1, 1)"), false, 'no free slot beside the shown grid');
  assert.equal(run("slotFits('folder0', occupied, 5, 3, 1, 1)"), true);
  assert.equal(run("slotFits('screensaver', occupied, 6, 4, 1, 1)"), true);
  assert.equal(run("tileTakesRoom({type: 5, col: 0, row: 0})"), true);
  assert.equal(run("tileTakesRoom({type: 5, layout_hidden: true})"), false, 'a tile without a place takes no room');
  assert.equal(run("tileTakesRoom({type: 0})"), false);
}

// --- The window's places (Tab5: classic 7 x 4, bar 5 x 3, upright 3 x 6).
const windowNames = ['layoutClone', 'layoutEmptyTile', 'layoutUsed', 'layoutNavType', 'layoutCanvas',
  'layoutInside', 'layoutFolderId', 'classicHidden', 'layoutBase', 'layoutSettle', 'takeoverTiles', 'setupTiles',
  'isCompactSensorType', 'isEditableValueType', 'supportsHalfSize', 'supportedTileLayout', 'saveLayoutWindow', 'refreshLayoutSave', 'layoutTabs',
  'layoutSignature', 'layoutDirty', 'layoutSelectedTile', 'takeOutLayoutTile'];
function windowContext(pages, data, extra = {}) {
  const ctx = vm.createContext({
    LAYOUT_KEYS: ['classic', 'bar', 'portrait'],
    LAYOUTS: {
      classic: {name: 'Klassisch', cols: 7, rows: 4, bar: false, portrait: false, available: true, switchable: true},
      bar: {name: 'Kopfleiste quer', cols: 5, rows: 3, bar: true, portrait: false, available: true, switchable: true},
      portrait: {name: 'Kopfleiste hochkant', cols: 3, rows: 6, bar: true, portrait: true, available: true, switchable: false},
    },
    ACTIVE_LAYOUT: 'classic', MEDIA_TILE_TYPE: 15, MEDIA_TILE_MIN_SPAN: 2, MEDIA_TILE_MAX_SPAN: 4, GRID_COLS: 7, GRID_ROWS: 6,
    tileTabs: Object.keys(pages), tilesData: {}, layoutWindow: {pages, data, key: 'bar', missingFolders: 0, busy: false},
    getTilesData: tab => ctx.tilesData[tab] || [], getFolderIdForTab: tab => Number(tab.slice('folder'.length)),
    isScreensaverTileTab: tab => tab === 'screensaver', document: {getElementById: () => ({}), querySelector: () => null},
    t: key => key, escapeHtml: value => String(value), showNotification: () => {}, ...extra,
  });
  vm.runInContext(windowNames.map(extractDeliveredFunction).join('\n'), ctx);
  return ctx;
}
const tile = (type, col, row, w, h, view, title = '') => ({type, col, row, span_w: w, span_h: h, view_id: view, title});
const place = list => JSON.parse(JSON.stringify(list.map(t => t && t.type ? [t.type, t.col, t.row, t.span_w, t.span_h] : 0)));

// Home: two folders in the first row, a sensor, the Settings tile; one tile
// made under a bar layout without a classic place.
const home = [tile(4, 0, 0, 1, 1, 11, 'Radio'), tile(4, 1, 0, 1, 1, 12, 'Licht'), tile(1, 0, 1, 2, 1, 13),
  tile(7, 6, 3, 1, 0.5, 14), tile(5, 3, 2, 1, 1, 15), {type: 0}];
const classic = {0: [[0, 11, 0, 0, 1, 1], [1, 12, 1, 0, 1, 1], [2, 13, 0, 1, 2, 1], [3, 14, 6, 3, 1, 0.5], [4, 15, 0, 0, 1, 1]]};
{
  const ctx = windowContext({folder0: home}, {classic, places: {classic_hidden: {0: [15]}}});
  const run = code => vm.runInContext(code, ctx);
  assert.deepEqual(JSON.parse(JSON.stringify(run('layoutCanvas()'))), {cols: 7, rows: 6}, 'one area for every layout');
  assert.equal(run("layoutInside({col: 0, row: 0, span_w: 1, span_h: 1}, 'classic')"), true, 'every screen starts under the head');
  assert.equal(run("layoutInside({col: 0, row: 3.5, span_w: 1, span_h: 1}, 'classic')"), false);
  assert.equal(run("layoutInside({col: 2, row: 5, span_w: 1, span_h: 1}, 'portrait')"), true);

  // Into a bar layout: every tile as it is in the classic layout, from the
  // top left of the new screen (folders too, no pills); Settings stays with
  // the classic layout; the tile without a classic place gets a free spot
  // beside the screen.
  const bar = run("takeoverTiles('folder0', 'bar', 'classic')");
  assert.deepEqual(place(bar).slice(0, 4), [[4, 0, 0, 1, 1], [4, 1, 0, 1, 1], [1, 0, 1, 2, 1], 0]);
  assert.equal(run(`layoutInside(${JSON.stringify(bar[4])}, 'bar')`), false, 'red beside the bar screen');
  // The classic layout in the window: its places; the hidden tile beside
  // the screen, nothing overlaps.
  const classicTiles = run("setupTiles('folder0', 'classic')");
  assert.deepEqual(place(classicTiles).slice(0, 4), [[4, 0, 0, 1, 1], [4, 1, 0, 1, 1], [1, 0, 1, 2, 1], [7, 6, 3, 1, 0.5]]);
  assert.equal(run(`layoutInside(${JSON.stringify(classicTiles[4])}, 'classic')`), false);
}
// A page without folders: the same places.
{
  const page = [tile(1, 0, 1, 2, 1, 21), tile(5, 2, 0, 1, 1, 22)];
  const ctx = windowContext({folder3: page}, {classic: {3: [[0, 21, 0, 1, 2, 1], [1, 22, 2, 0, 1, 1]]}, places: {}});
  assert.deepEqual(place(vm.runInContext("takeoverTiles('folder3', 'bar', 'classic')", ctx)),
    [[1, 0, 1, 2, 1], [5, 2, 0, 1, 1]]);
}
// A saved bar layout: its places; a tile without one (made under the
// classic layout) beside the screen. Taken over into the classic layout,
// every tile keeps its place on the screen; Settings keeps its classic one.
{
  const places = {bar: {0: {11: [0, 0, 1, 0.5], 12: [1, 0, 1, 0.5], 13: [0, 1, 2, 1]}}};
  const ctx = windowContext({folder0: home}, {classic, places});
  const run = code => vm.runInContext(code, ctx);
  const saved = run("setupTiles('folder0', 'bar')");
  assert.deepEqual(place(saved).slice(0, 4), [[4, 0, 0, 1, 0.5], [4, 1, 0, 1, 0.5], [1, 0, 1, 2, 1], 0]);
  assert.equal(run(`layoutInside(${JSON.stringify(saved[4])}, 'bar')`), false);
  const back = run("takeoverTiles('folder0', 'classic', 'bar')");
  assert.equal(run(`layoutInside(${JSON.stringify(back[0])}, 'classic')`), true, 'the same place on the classic screen');
  assert.deepEqual(place(back).slice(0, 4), [[4, 0, 0, 1, 0.5], [4, 1, 0, 1, 0.5], [1, 0, 1, 2, 1], [7, 6, 3, 1, 0.5]]);
}

// The cross takes a tile out of this layout only: beside the screen (red),
// without a request (its places in the other layouts stay); not for folders.
{
  const calls = [];
  const ctx = windowContext({folder0: home}, {classic, places: {}}, {
    currentTileTab: 'folder0', currentTileIndex: 2, drafts: {folder0: {2: {col: '1'}}},
    showLayoutTiles: () => calls.push('shown'), layoutWindowFetch: () => calls.push('fetch'),
  });
  ctx.document.querySelectorAll = () => [];
  ctx.layoutWindow.tab = 'folder0';
  ctx.tilesData.folder0 = vm.runInContext("setupTiles('folder0', 'bar')", ctx);
  const run = code => vm.runInContext(code, ctx);
  assert.equal(run("layoutInside(getTilesData('folder0')[2], 'bar')"), true);
  run('takeOutLayoutTile()');
  assert.equal(run("layoutInside(getTilesData('folder0')[2], 'bar')"), false, 'the tile is red beside the screen');
  assert.equal(run("getTilesData('folder0')[2].type"), 1, 'it is not deleted');
  assert.deepEqual(calls, ['shown'], 'nothing is sent to the panel');
  assert.equal(ctx.drafts.folder0[2], undefined);
  ctx.currentTileIndex = 0;
  assert.equal(run('layoutSelectedTile()'), null, 'a folder cannot be taken out');
}

// "Speichern": places by tile ID in the layout's own rows; Settings and Back
// are no part of a bar layout; nothing is stored while a folder is red.
{
  const calls = [];
  const fetchStub = async (url, options) => {
    calls.push([url, options?.body]);
    return {ok: true, json: async () => ({success: true, places: {}})};
  };
  const ctx = windowContext({folder0: home}, {classic, places: {}}, {layoutWindowFetch: fetchStub});
  ctx.tilesData.folder0 = [tile(4, 0, 0, 1, 1, 11), tile(4, 1, 0, 1, 1, 12), tile(1, 0, 1, 2, 1, 13),
    tile(7, 6, 3, 1, 0.5, 14), {type: 0}, {type: 0}];
  ctx.layoutWindow.key = 'classic';
  assert.equal(await vm.runInContext('saveLayoutWindow()', ctx), true);
  assert.deepEqual(JSON.parse(calls[0][1]), {layout: 'classic', folders: {0: {11: [0, 0, 1, 1], 12: [1, 0, 1, 1],
    13: [0, 1, 2, 1], 14: [6, 3, 1, 0.5]}}}, 'classic places, Settings included');
  calls.length = 0;
  ctx.layoutWindow.key = 'bar';
  await vm.runInContext('saveLayoutWindow()', ctx);
  assert.equal(JSON.parse(calls[0][1]).folders[0][14], undefined, 'no Settings tile in a bar layout');
  calls.length = 0;
  ctx.layoutWindow.missingFolders = 1;
  assert.equal(await vm.runInContext('saveLayoutWindow()', ctx), false);
  assert.equal(calls.length, 0, 'a red folder blocks saving');
}

// The editor in the window talks to nobody (user 2026-10-08: thrown out of
// the window after every move): no tile save, no reorder request, no tile
// reload from the panel, no language reload.
const windowGuard = /if \(typeof layoutWindowOpen === 'function' && layoutWindowOpen\(\)\)/;
const autosave = readRepoFile('src/web/admin/tiles/autosave.js');
const saveBody = autosave.slice(autosave.indexOf('function saveTile('));
assert.ok(saveBody.search(windowGuard) >= 0 && saveBody.search(windowGuard) < saveBody.indexOf("fetch('/api/tiles'"),
  'a tile save stays in the window');
const reorder = readRepoFile('src/web/admin/tiles/drag-resize.js');
assert.ok(reorder.search(windowGuard) >= 0 && reorder.search(windowGuard) < reorder.indexOf("fetch('/api/tiles/reorder'"),
  'a move stays in the window');
assert.match(readRepoFile('src/web/admin/tiles/grid-preview.js'),
  /if \(typeof layoutWindowOpen === 'function' && layoutWindowOpen\(\)\) refreshTiles = false;/);
assert.match(readRepoFile('src/web/admin/core/localization.js'),
  /if \(typeof layoutWindowOpen === 'function' && layoutWindowOpen\(\)\) return true;/);

// --- Server, boot and page.
const tiles = readRepoFile('src/web/server/handlers/web_admin_tiles.cpp');
assert.match(tiles, /if \(!placed\) return "Folder without place";/, 'a folder without a place is refused');
assert.match(tiles, /return "Tile overlaps";/);
assert.match(tiles, /tile_layouts::set_classic_hidden\(folder_id, tile\.view_id, entry == places\.end\(\)\);/,
  'a classic tile beside the screen has no classic place');
assert.match(tiles, /!layoutFromKey\(server\.arg\("layout"\), layout\) \|\| !grid_layout::switchable\(layout\)/,
  'only a layout the panel can show is chosen');
assert.match(tiles, /const bool classic_places = !screensaver_grid && server\.arg\("layout"\) == "classic";/,
  'export and import use the classic places');
assert.match(tiles, /if \(tile\.layout_hidden\) out \+= ",\\"layout_hidden\\":true";/);
const routes = readRepoFile('src/web/server/web_admin.cpp');
assert.match(routes, /"\/api\/layouts", HTTP_POST,\s*guarded\(withStorageHold\(\[this\]\(\) \{ this->handleSaveLayouts\(\); \}\)\)\);/);
assert.match(routes, /"\/api\/layouts\/active", HTTP_POST,/);
assert.doesNotMatch(routes, /head-bar/, 'the old switch is gone');
const config = readRepoFile('src/core/config/config_manager.cpp');
assert.match(config, /config\.layout = prefs\.getUChar\("layout", 0\);/,
  'the panel starts classic until the window sets up and chooses a layout');
assert.match(readRepoFile('src/tiles/config/tile_config.cpp'),
  /!tile_layouts::has_layout\(grid_layout::active\(\)\)\) \{[\s\S]*?grid_layout::apply\(0\);/,
  'a bar layout without places starts classic');
assert.match(readRepoFile('HomeTiles.ino'), /grid_layout::apply\(configManager\.getConfig\(\)\.layout\);/);
const tileConfig = readRepoFile('src/tiles/config/tile_config.cpp');
assert.match(tileConfig, /if \(layout_places && folder_id != kScreensaverGridStorageId\) applyLayoutPlaces\(folder_id, grid\);/);
assert.match(tileConfig, /resolveLayoutPlaces\(folder_id, working, !layout_places, classic\.get\(\)\);/,
  'the tile data keeps the classic places');
const html = readRepoFile('src/web/server/render/web_admin_html.cpp');
assert.match(html, /onclick=\\"openLayoutWindow\(\)\\"/);
assert.match(html, /cssClass \+= " tile-unplaced";/);
const scripts = readRepoFile('src/web/server/render/web_admin_scripts.cpp');
assert.match(scripts, /let GRID_COLS = /, 'the window lays out its area with the same editor');
assert.match(scripts, /const LAYOUTS = \{/);
const i18n = readRepoFile('src/core/i18n/i18n.cpp');
for (const text of ['"Layout ändern",', '"Change layout",', '"Changer la disposition",', '"Zmień układ",']) {
  assert.ok(i18n.includes(text), text);
}

console.log('Layouts: editor grid, window take-over, red places, saving and server checks pass');
