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
  assert.equal(run("placeCols('screensaver')"), 6, 'the screensaver takes the shown grid (screensaver_places.h)');
  run('var occupied = Array.from({length: 10}, () => Array(14).fill(false));');
  assert.equal(run("slotFits('folder0', occupied, 6, 0, 1, 1)"), false, 'no free slot beside the shown grid');
  assert.equal(run("slotFits('folder0', occupied, 5, 3, 1, 1)"), true);
  assert.equal(run("slotFits('screensaver', occupied, 5, 3, 1, 1)"), true, 'the bottom rows of the shown grid');
  assert.equal(run("slotFits('screensaver', occupied, 6, 4, 1, 1)"), false);
  assert.equal(run("tileTakesRoom({type: 5, col: 0, row: 0})"), true);
  assert.equal(run("tileTakesRoom({type: 5, layout_hidden: true})"), false, 'a tile without a place takes no room');
  assert.equal(run("tileTakesRoom({type: 0})"), false);
}

// --- The window's places (Tab5: classic 7 x 4, bar 5 x 3, upright 3 x 6).
const windowNames = ['layoutClone', 'layoutEmptyTile', 'layoutUsed', 'layoutNavType', 'layoutBarHidden', 'layoutCanvas',
  'layoutInside', 'layoutFolderId', 'classicHidden', 'layoutBase', 'layoutSettle', 'takeoverTiles', 'setupTiles',
  'isCompactSensorType', 'isEditableValueType', 'supportsHalfSize', 'supportedTileLayout', 'saveLayoutWindow', 'refreshLayoutSave', 'layoutTabs',
  'layoutSignature', 'layoutSavedSig', 'layoutDirty', 'layoutSelectedTile', 'deselectLayoutTile', 'normalizeLayoutForTileType', 'clampHalf',
  'headBarLayout', 'placeCols', 'placeRows',
  'deleteLayoutTile', 'classicParked', 'layoutParked', 'layoutRed', 'loadLayoutWindow', 'mayLeaveLayout',
  'layoutClockFrom', 'layoutClock', 'layoutClockDirty'];
function windowContext(pages, data, extra = {}) {
  // Every case gets its own copy: the window changes the pages it holds.
  pages = JSON.parse(JSON.stringify(pages));
  const ctx = vm.createContext({
    LAYOUT_KEYS: ['classic', 'bar', 'portrait'],
    LAYOUTS: {
      classic: {name: 'Klassisch', cols: 7, rows: 4, bar: false, portrait: false, available: true, switchable: true},
      bar: {name: 'Kopfleiste quer', cols: 5, rows: 3, bar: true, portrait: false, available: true, switchable: true},
      portrait: {name: 'Kopfleiste hochkant', cols: 3, rows: 6, bar: true, portrait: true, available: true, switchable: false},
    },
    ACTIVE_LAYOUT: 'classic', MEDIA_TILE_TYPE: 15, MEDIA_TILE_MIN_SPAN: 2, MEDIA_TILE_MAX_SPAN: 4, GRID_COLS: 7, GRID_ROWS: 6,
    tileTabs: Object.keys(pages), tilesData: {}, currentTileTab: 'folder0',
    layoutWindow: {pages, data, key: 'bar', missingFolders: 0, busy: false, shown: false, work: {}, saved: {}},
    refreshLayoutStatus: () => {}, refreshLayoutButtons: () => {}, mountLayoutTab: async () => {}, useLayoutGrid: () => {},
    showLayoutTiles: (tab, tiles) => { ctx.tilesData[tab] = tiles; },
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
  const ctx = windowContext({folder0: home}, {classic, places: {classic_parked: {0: {15: [0, 0, 0, 0]}}}});
  const run = code => vm.runInContext(code, ctx);
  assert.deepEqual(JSON.parse(JSON.stringify(run('layoutCanvas()'))), {cols: 7, rows: 6}, 'one area for every layout');
  // The square panels (every layout 3 x 3): the screen in the corner, two
  // columns of storage to the right and two rows below.
  const tab5Layouts = ctx.LAYOUTS;
  ctx.LAYOUTS = {classic: {cols: 3, rows: 3, available: true}, bar: {cols: 3, rows: 3, available: true},
                 portrait: {cols: 3, rows: 3, available: false}};
  assert.deepEqual(JSON.parse(JSON.stringify(run('layoutCanvas()'))), {cols: 5, rows: 5}, 'room beside small screens');
  ctx.LAYOUTS = tab5Layouts;
  assert.equal(run("layoutInside({col: 0, row: 0, span_w: 1, span_h: 1}, 'classic')"), true, 'every screen starts under the head');
  assert.equal(run("layoutInside({col: 0, row: 3.5, span_w: 1, span_h: 1}, 'classic')"), false);
  assert.equal(run("layoutInside({col: 2, row: 5, span_w: 1, span_h: 1}, 'portrait')"), true);

  // Every screen in the area's top left corner, the storage to the right
  // and below only.
  assert.equal(run("layoutParked({col: 0, row: 3, span_w: 1, span_h: 1}, 'bar')"), true, 'below');
  assert.equal(run("layoutParked({col: -1, row: 0, span_w: 1, span_h: 1}, 'bar')"), false, 'nothing on the left');
  // Into a bar layout: every tile as it is in the classic layout, from the
  // top left of the new screen (folders too, no pills), the Settings tile
  // too (here beside the smaller screen: in the storage); the tile without a
  // classic place gets a free spot beside the screen.
  const bar = run("takeoverTiles('folder0', 'bar', 'classic')");
  assert.deepEqual(place(bar).slice(0, 3), [[4, 0, 0, 1, 1], [4, 1, 0, 1, 1], [1, 0, 1, 2, 1]]);
  assert.equal(run(`layoutParked(${JSON.stringify(bar[3])}, 'bar')`), true, 'Settings beyond the smaller screen: stored');
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
// every tile keeps its place on the screen; a parked one is parked there too.
{
  const places = {bar: {0: {11: [0, 0, 1, 0.5], 12: [1, 0, 1, 0.5], 13: [0, 1, 2, 1]}}};
  const ctx = windowContext({folder0: home}, {classic, places});
  const run = code => vm.runInContext(code, ctx);
  const saved = run("setupTiles('folder0', 'bar')");
  assert.deepEqual(place(saved).slice(0, 3), [[4, 0, 0, 1, 0.5], [4, 1, 0, 1, 0.5], [1, 0, 1, 2, 1]]);
  assert.equal(run(`layoutParked(${JSON.stringify(saved[3])}, 'bar')`), true, 'Settings without a place: in the storage');
  assert.equal(run(`layoutInside(${JSON.stringify(saved[4])}, 'bar')`), false);
  const back = run("takeoverTiles('folder0', 'classic', 'bar')");
  assert.equal(run(`layoutInside(${JSON.stringify(back[0])}, 'classic')`), true, 'the same place on the classic screen');
  assert.deepEqual(place(back).slice(0, 3), [[4, 0, 0, 1, 0.5], [4, 1, 0, 1, 0.5], [1, 0, 1, 2, 1]]);
  assert.equal(run(`layoutParked(${JSON.stringify(back[3])}, 'classic')`), true, 'parked there, parked here');
}

// The storage beside a screen: a tile wholly there is parked (dimmed), half
// over the edge it is red; a folder outside the screen is always red. The
// cross parks a tile on the screen without a request, and deletes a parked
// one from every layout; never a folder.
{
  const calls = [];
  const ctx = windowContext({folder0: home}, {classic, places: {}}, {
    currentTileTab: 'folder0', currentTileIndex: 2, drafts: {folder0: {2: {col: '1'}}},
    layoutWindowFetch: async (url, options) => { calls.push([url, [...options.body.entries()]]); return {ok: true}; },
    FormData: class { constructor() { this.list = []; } append(k, v) { this.list.push([k, v]); } entries() { return this.list; } },
    tf: (key, values) => key + JSON.stringify(values), getTileTypeMeta: () => ({label: 'Sensor'}),
    renderTileFromData: () => {}, layoutTiles: () => {}, sensorMetaCache: {}, window: {confirm: () => true},
  });
  ctx.document.querySelectorAll = () => [];
  ctx.layoutWindow.tab = 'folder0';
  ctx.tilesData.folder0 = vm.runInContext("setupTiles('folder0', 'bar')", ctx);
  const run = code => vm.runInContext(code, ctx);
  assert.equal(run("layoutRed({type: 1, col: 4.5, row: 0, span_w: 1, span_h: 1}, 'bar')"), true, 'half over the edge');
  assert.equal(run("layoutRed({type: 1, col: 5, row: 0, span_w: 1, span_h: 1}, 'bar')"), false, 'in the storage');
  assert.equal(run("layoutParked({col: 5, row: 0, span_w: 1, span_h: 1}, 'bar')"), true);
  assert.equal(run("layoutRed({type: 4, col: 5, row: 0, span_w: 1, span_h: 1}, 'bar')"), true, 'a folder needs a place');
  // The cross on a tile on the screen asks and deletes it too (user
  // 2026-10-08); into the storage a tile is dragged.
  assert.equal(run("layoutInside(getTilesData('folder0')[2], 'bar')"), true);
  ctx.currentTileIndex = 2;
  await run('deleteLayoutTile()');
  assert.deepEqual(calls, [['/api/tiles', [['folder', '0'], ['index', '2'], ['type', '0']]]], 'the tile is deleted');
  assert.equal(run("getTilesData('folder0')[2].type"), 0);
  ctx.currentTileIndex = 0;
  assert.equal(run('layoutSelectedTile()'), null, 'no cross on a folder');
}

// Switching layouts keeps the unsaved places of the one left, without a
// question; closing asks while any layout is not saved.
{
  const ctx = windowContext({folder0: home}, {classic, places: {}}, {window: {confirm: () => false}});
  ctx.tf = (key, values) => key + ':' + values.layout;
  const run = code => vm.runInContext(code, ctx);
  await run("loadLayoutWindow('bar', '')");
  run("getTilesData('folder0')[2].col = 3");
  assert.equal(run("layoutDirty('bar')"), true);
  await run("loadLayoutWindow('classic', '')");
  assert.equal(run("layoutDirty('bar')"), true, 'the bar layout keeps its unsaved move');
  assert.equal(run("layoutDirty('classic')"), false);
  await run("loadLayoutWindow('bar', '')");
  assert.equal(run("getTilesData('folder0')[2].col"), 3, 'and shows it again');
  assert.equal(run('mayLeaveLayout()'), false, 'closing asks');
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
  ctx.layoutWindow.shown = true;
  assert.equal(await vm.runInContext('saveLayoutWindow()', ctx), true);
  assert.deepEqual(JSON.parse(calls[0][1]), {layout: 'classic', folders: {0: {11: [0, 0, 1, 1], 12: [1, 0, 1, 1],
    13: [0, 1, 2, 1], 14: [6, 3, 1, 0.5]}}}, 'classic places, Settings included');
  calls.length = 0;
  ctx.layoutWindow.key = 'bar';
  await vm.runInContext('saveLayoutWindow()', ctx);
  assert.deepEqual(JSON.parse(calls[0][1]).folders[0][14], [6, 3, 1, 0.5], 'the Settings tile is part of a bar layout too');
  calls.length = 0;
  ctx.layoutWindow.blocked = true;
  assert.equal(await vm.runInContext('saveLayoutWindow()', ctx), false);
  assert.equal(calls.length, 0, 'red (a folder without a place, a tile over the edge) blocks saving');
}

// The screensaver clock per layout (user 2026-10-08): each layout's place
// and size, unsaved until "Speichern" (then sent with the places), taken
// along by "Kopieren von".
{
  const calls = [];
  const clock = {classic: [500, 350, 48, 28], bar: [500, 350, 48, 28], portrait: [500, 200, 64, 32]};
  const fetchStub = async (url, options) => {
    calls.push([url, options?.body]);
    return {ok: true, json: async () => ({success: true, classic, places: {}, clock})};
  };
  const ctx = windowContext({folder0: home}, {classic, places: {}, clock}, {layoutWindowFetch: fetchStub});
  const run = code => vm.runInContext(code, ctx);
  await run("loadLayoutWindow('bar', '')");
  assert.equal(run("layoutDirty('bar')"), false);
  run("layoutClock('bar').clock_y = 800");
  assert.equal(run("layoutDirty('bar')"), true, 'a moved clock is unsaved');
  assert.equal(run("layoutDirty('portrait')"), false, 'the other layouts keep theirs');
  assert.equal(await run('saveLayoutWindow()'), true);
  assert.deepEqual(JSON.parse(calls[0][1]).clock, [500, 800, 48, 28], 'sent with the places');
  clock.bar = [500, 800, 48, 28];
  assert.equal(run("layoutDirty('bar')"), false);
  ctx.layoutWindow.shown = true;
  await run("loadLayoutWindow('bar', 'portrait')");
  assert.deepEqual(JSON.parse(JSON.stringify(run("layoutClock('bar')"))),
    {clock_x: 500, clock_y: 200, time_font_size: 64, date_font_size: 32}, 'Kopieren von takes the clock along');
  calls.length = 0;
  await run('saveLayoutWindow()');
  assert.deepEqual(JSON.parse(calls[0][1]).clock, [500, 200, 64, 32]);
}
const windowSource = readRepoFile('src/web/admin/tiles/layout-window.js');
assert.ok(windowSource.includes('if (isScreensaverTileTab(tab) && typeof screensaverLoaded !== \'undefined\' && screensaverLoaded) renderScreensaverEditor();'),
  'the window draws the clock at its layout\'s place');
const screensaverEditor = readRepoFile('src/web/admin/screensaver/editor.js');
assert.ok(screensaverEditor.includes('return (layoutWindowScreen(preview) && layoutWindowClock()) || screensaverDraft;'),
  'the clock in the window is the layout\'s, on the page the active one\'s');
assert.ok(screensaverEditor.includes("clock.style.left = (screen.left + place.clock_x * screen.width / 1000) + 'px';"));

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
assert.match(tiles, /tile_layouts::set\(grid_layout::Layout::kClassic, folder_id, tile\.view_id, entry->place\);/,
  'a classic tile in the storage keeps its spot there and has no classic place');
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
