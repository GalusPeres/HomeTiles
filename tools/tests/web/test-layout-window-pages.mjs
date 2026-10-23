// The layout window's pages (user 2026-10-12: the window lacked the page
// slots, so the page folders were simply missing there; 2026-10-13: Home's
// own fields, test-layout-window-fields.mjs). A folder dropped in leaves
// Home's area in both bar layouts and stands in its field, a page dragged
// onto the area is a folder tile again. The window keeps the pages like its
// places and "Speichern" stores both in one request; the panel gives a
// folder back from the pages a spot in the other bar layout, and refuses
// when the layout it shows has no room.
import assert from 'node:assert/strict';
import vm from 'node:vm';
import {extractDeliveredFunction, readRepoFile} from '../../lib/admin-source.mjs';

const names = ['layoutClone', 'layoutEmptyTile', 'layoutUsed', 'layoutShown', 'layoutNavType', 'layoutBarHidden',
  'layoutBarTile', 'layoutWindowPages', 'layoutCanvas',
  'layoutInside', 'layoutFolderId', 'classicHidden', 'classicParked', 'layoutParked', 'layoutBase', 'layoutSettle',
  'takeoverTiles', 'setupTiles', 'isCompactSensorType', 'isEditableValueType', 'supportsHalfSize', 'supportedTileLayout',
  'normalizeLayoutForTileType', 'clampHalf', 'headBarLayout', 'placeCols', 'placeRows', 'layoutTabs', 'layoutSignature',
  'layoutSavedSig', 'layoutDirty', 'layoutMustReach', 'layoutRed', 'loadLayoutWindow', 'saveLayoutWindow', 'refreshLayoutSave',
  'layoutClockFrom', 'layoutClock', 'layoutClockDirty', 'layoutPageSlotsFrom', 'layoutHomeTab', 'layoutHomeFolderIndex',
  'layoutPagesDirty', 'reconcileLayoutPages', 'layoutPageOut', 'layoutParkSettings', 'layoutSettingsParkedFrom',
  'layoutSettingsIndex', 'layoutSettingsIn', 'layoutSettingsOut', 'layoutWindowSettingsParked'];

const tile = (type, col, row, w, h, view, extra = {}) => ({type, col, row, span_w: w, span_h: h, view_id: view, ...extra});
// Home: Lighting and MISC are pages, Climate a folder tile; places as the
// Guition V2 has them (classic 7 x 5, bar 6 x 4, upright 4 x 6.5).
const home = [tile(4, 0, 0, 1, 1, 101, {navigate_target: 1, title: 'Lighting'}),
  tile(4, 1, 0, 1, 1, 102, {navigate_target: 2, title: 'MISC'}),
  tile(4, 2, 0, 1, 1, 103, {navigate_target: 3, title: 'Climate'}),
  tile(2, 3, 0, 1, 1, 104, {title: 'Movie'}), {type: 0}];
const classic = {0: home.filter(t => t.type).map((t, slot) => [slot, t.view_id, t.col, t.row, 1, 1])};
const places = {pages: [1, 0, 2],
  bar: {0: {103: [0, 0, 1, 1], 104: [1, 0, 1, 1]}}, portrait: {0: {103: [0, 0, 1, 1], 104: [1, 0, 1, 1]}}};

function windowContext(extra = {}) {
  const ctx = vm.createContext({
    LAYOUT_KEYS: ['classic', 'bar', 'portrait'], PAGE_SLOT_COUNT: 5,
    LAYOUTS: {
      classic: {name: 'Klassisch', cols: 7, rows: 5, bar: false, available: true, switchable: true},
      bar: {name: 'Kopfleiste quer', cols: 6, rows: 4, bar: true, available: true, switchable: true},
      portrait: {name: 'Kopfleiste hochkant', cols: 4, rows: 6.5, bar: true, portrait: true, available: true, switchable: true},
    },
    ACTIVE_LAYOUT: 'bar', MEDIA_TILE_TYPE: 15, MEDIA_TILE_MIN_SPAN: 2, MEDIA_TILE_MAX_SPAN: 4, GRID_COLS: 7, GRID_ROWS: 7,
    tileTabs: ['folder0'], tabByFolder: {}, tilesData: {}, currentTileTab: 'folder0', currentTileIndex: -1, sensorMetaCache: {},
    refreshLayoutStatus: () => {}, refreshLayoutButtons: () => {}, mountLayoutTab: async () => {}, useLayoutGrid: () => {},
    markLayoutRed: () => {}, layoutPagesChanged: () => {},
    renderTileFromData: () => {}, layoutTiles: () => {},
    showLayoutTiles: (tab, tiles) => { ctx.tilesData[tab] = tiles; },
    getTilesData: tab => ctx.tilesData[tab] || [], getFolderIdForTab: tab => Number(tab.slice('folder'.length)),
    isScreensaverTileTab: () => false, document: {getElementById: () => ({}), querySelector: () => null}, t: key => key, showNotification: () => {},
    ...extra,
  });
  vm.runInContext(names.map(extractDeliveredFunction).join('\n'), ctx);
  const data = {success: true, classic, places};
  ctx.layoutWindow = {pages: {folder0: JSON.parse(JSON.stringify(home))}, data, key: 'bar', tab: 'folder0', busy: false,
    shown: false, work: {}, saved: {}, pageUnlock: false};
  vm.runInContext(`layoutWindow.pageSlots = layoutPageSlotsFrom(layoutWindow.data);
    layoutWindow.savedPageSlots = layoutWindow.pageSlots.slice();`, ctx);
  return ctx;
}
const placeOf = (list, view) => {
  const t = list.find(entry => entry && entry.view_id === view);
  return t && t.type && !t.layout_hidden ? [t.col, t.row, t.span_w, t.span_h] : null;
};

// The pages as stored; their own tiles stand in their fields, no part of a
// bar layout's area.
{
  const ctx = windowContext();
  const run = code => vm.runInContext(code, ctx);
  assert.deepEqual(JSON.parse(JSON.stringify(run('layoutWindow.pageSlots'))), [1, 0, 2, 0, 0], 'five places, a gap kept');
  await run("loadLayoutWindow('bar', '')");
  assert.equal(placeOf(run("getTilesData('folder0')"), 101), null, 'a page has no place on Home');
  assert.equal(run("getTilesData('folder0')[0].layout_hidden"), true, 'its own tile stands in its field');
  assert.equal(run("layoutShown(getTilesData('folder0')[0])"), false);
  assert.deepEqual(placeOf(run("getTilesData('folder0')"), 103), [0, 0, 1, 1]);
  assert.equal(run("layoutDirty('bar')"), false);
}

// A page onto the area, a folder into a place: unsaved in both bar layouts,
// not in the classic one; the other bar layout follows when it is shown.
{
  const ctx = windowContext();
  const run = code => vm.runInContext(code, ctx);
  await run("loadLayoutWindow('bar', '')");
  run("layoutPageOut(1, {col: 3, row: 1, span_w: 1, span_h: 1})");
  assert.deepEqual(JSON.parse(JSON.stringify(run('layoutWindow.pageSlots'))), [0, 0, 2, 0, 0], 'its place stays free');
  assert.deepEqual(placeOf(run("getTilesData('folder0')"), 101), [3, 1, 1, 1], 'where it was dropped');
  run("layoutWindowPages([0, 0, 2, 0, 3], false)");
  assert.equal(placeOf(run("getTilesData('folder0')"), 103), null, 'the new page went into its field');
  assert.equal(run("getTilesData('folder0')[2].layout_hidden"), true);
  assert.equal(run("layoutDirty('bar')"), true);
  assert.equal(run("layoutDirty('portrait')"), true, 'both bar layouts share the pages');
  assert.equal(run("layoutDirty('classic')"), false, 'the classic layout has none');
  await run("loadLayoutWindow('portrait', '')");
  const upright = run("getTilesData('folder0')");
  assert.equal(placeOf(upright, 103), null, 'the new page leaves Home there too');
  assert.deepEqual(placeOf(upright, 101), [0, 0, 1, 1], 'the folder back from the pages: the first free spot on the screen');
  await run("loadLayoutWindow('classic', '')");
  assert.deepEqual(placeOf(run("getTilesData('folder0')"), 103), [2, 0, 1, 1], 'classic keeps every folder');
}

// "Speichern": the pages with a bar layout's places in one request (after
// the PIN question: unlock), never with the classic places.
{
  const calls = [];
  const after = {pages: [0, 0, 2, 0, 3], bar: {0: {101: [3, 1, 1, 1], 104: [1, 0, 1, 1]}},
    portrait: {0: {101: [0, 0, 1, 1], 104: [1, 0, 1, 1]}}};
  const ctx = windowContext({layoutWindowFetch: async (url, options) => {
    calls.push([url, options?.body]);
    return {ok: true, json: async () => ({success: true, classic, places: after})};
  }});
  const run = code => vm.runInContext(code, ctx);
  await run("loadLayoutWindow('portrait', '')");
  await run("loadLayoutWindow('bar', '')");
  run("layoutPageOut(1, {col: 3, row: 1, span_w: 1, span_h: 1})");
  run("layoutWindowPages([0, 0, 2, 0, 3], true)");
  assert.equal(await run('saveLayoutWindow()'), true);
  const body = JSON.parse(calls[0][1]);
  assert.deepEqual(body.pages, [0, 0, 2, 0, 3], 'the slots, a free one as 0');
  assert.equal(body.unlock, true);
  assert.deepEqual(body.folders[0], {101: [3, 1, 1, 1], 104: [1, 0, 1, 1]}, 'no place for a page');
  assert.equal(run("layoutDirty('bar')"), false, 'stored');
  assert.equal(run("layoutDirty('portrait')"), false, 'the other bar layout counts against the panel\'s new places');
  assert.equal(run('layoutWindow.changed'), true, 'the page reloads the panel\'s tiles on close');
  calls.length = 0;
  run("layoutWindow.pageSlots = [2, 0, 0, 0, 3]");
  ctx.layoutWindow.key = 'classic';
  await run('saveLayoutWindow()');
  assert.equal(JSON.parse(calls[0][1]).pages, undefined, 'never with the classic places');
}

// The fields in the window: no row of its own, the drops of pages stay in
// the window, the heads show the window's pages, the editor's pages return
// on close.
const windowJs = readRepoFile('src/web/admin/tiles/layout-window.js');
assert.doesNotMatch(windowJs, /setup-pages|page-slot\b|bindLayoutPages|renderLayoutPages/);
assert.match(windowJs, /ensurePageFields\(\);/, 'a panel showing the classic layout gets the fields in the window');
assert.match(windowJs, /holder\.dataset\.pages = encodePageSlots\(layoutWindow\.pageSlots\);/);
assert.match(windowJs, /if \(pageSlots\) pageSlots\.dataset\.pages = encodePageSlots\(state\.savedPageSlots\);/);
const pageSlots = readRepoFile('src/web/admin/tiles/page-fields.js');
assert.match(pageSlots, /if \(pageFieldsInWindow\(\)\) \{\s*layoutPageOut\(folder, place\);\s*return;\s*\}/,
  'a page dropped on the window\'s area is not sent to /api/pages');
assert.match(pageSlots, /if \(pageFieldsInWindow\(\)\) layoutWindowPages\(places, unlock\);\s*else savePages\(places, \{\}, unlock\);/,
  'nor a page dropped on a field');
assert.match(pageSlots, /function syncPageHeads\(pages = pageFolders\(\)\) \{/);
const css = readRepoFile('src/web/assets/admin.css');
assert.doesNotMatch(css, /\.setup-pages|\.page-slot\b|\.page-parking/);
assert.match(css, /\.tile-grid\.setup-grid::before \{[^}]*right:calc\(var\(--setup-room-right, 0px\) - 1px\);/);
assert.match(readRepoFile('src/web/server/render/web_admin_scripts.cpp'), /appendJsEntry\("pagesTitle", tr\.pages_title\);/);

// The panel: the slots checked like /api/pages, a locked folder refused
// without the question, a folder back from the pages placed in the other bar
// layout, refused when the panel shows that layout and it has no room.
const server = readRepoFile('src/web/server/handlers/web_admin_tiles.cpp');
const save = server.slice(server.indexOf('void WebAdminServer::handleSaveLayouts() {'), server.indexOf('// POST /api/layouts/active'));
assert.match(save, /JsonArrayConst sent_pages = doc\["pages"\]\.as<JsonArrayConst>\(\);/);
assert.match(save, /if \(const char\* error = checkPageSlots\(pages\)\) \{/);
assert.match(save, /lockedNewPage\(pages, before, before_count, doc\["unlock"\] \| false\)/);
assert.match(save, /readFolderPlaces\(folder\.value\(\)\.as<JsonObjectConst>\(\), \*grid, screen,\s*home_pages \? &pages : nullptr, places\)/,
  'a page needs no place, a folder back from the pages does');
// A folder dragged in is a 1x1 tile and comes back as one (user 2026-10-12).
assert.match(save, /if \(findFreeLayoutPlace\(other_screen, taken, 1, 1, place\)\) \{/);
assert.doesNotMatch(server, /own_size/);
assert.match(save, /\} else if \(other == grid_layout::active\(\)\) \{\s*\/\/ The panel shows that layout: the folder would be out of reach\.\s*sendJsonError\(server, 409, "No free cell"\);/);
assert.match(save, /tile_layouts::set_pages\(pages\.data\(\), pages\.size\(\)\);/);
assert.match(save, /if \(pages_changed && grid_layout::active\(\) != grid_layout::Layout::kClassic\) \{\s*tiles_rebuild_heads\(!powerManager\.isInSleep\(\)\);/);
const pagesHandler = server.slice(server.indexOf('void WebAdminServer::handleSavePages() {'));
assert.match(pagesHandler, /if \(const char\* error = checkPageSlots\(wanted\)\) \{/, 'one check for both ways');
assert.match(pagesHandler, /const tile_layouts::Place dropped\{server\.arg\("col"\)\.toFloat\(\), server\.arg\("row"\)\.toFloat\(\), 1, 1\};[\s\S]*?if \(!placed\) placed = findFreeShownPlace\(\*home, \*tile, 1, 1, place\);/);
// A page out of its field is one cell, the window's parked Settings tile
// too (user 2026-10-22: it came out half high).
assert.match(pageSlots, /kind: 'page-slot', folder: Number\(tile\.dataset\.navigateTarget \|\| 0\), settings,\s*(?:\/\/[^\n]*\n\s*)*layout: \{col: 0, row: 0, span_w: 1, span_h: 1\}/);
assert.match(pageSlots, /const place = \{col: candidate\.col, row: candidate\.row, span_w: 1, span_h: 1\};/);
// A new folder goes into a page slot at once: the save tells its tile the
// folder (user 2026-10-12: only after a few clicks; an autosave draws
// nothing again).
assert.match(readRepoFile('src/web/admin/tiles/autosave.js'),
  /const tileEl = document\.getElementById\(tab \+ '-tile-' \+ tileIndex\);\s*if \(tileEl\) tileEl\.dataset\.navigateTarget = resolvedNavTarget;/);
console.log('Layout window pages: kept like the places, stored with them');
