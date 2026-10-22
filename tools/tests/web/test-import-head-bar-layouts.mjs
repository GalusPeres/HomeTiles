// A backup from a panel with the head bar (V2, 1280 x 800) did not import
// into another one with the head bar (8-inch, same screen, 2026-10-21): the
// import checked the export's classic places (7 x 5) against the head bar's
// shown grid (6 x 4) and stopped at the first tile in the seventh column.
// And a backup has to carry every layout together (user 2026-10-21): the
// classic places, the bar and upright places of every folder and of the
// screensaver, the pages, the screensaver clock per layout and where
// Settings is parked. The real export and import code runs here between two
// fake panels with the firmware's rules (handleSaveTiles with layout=classic,
// handleSaveLayouts).
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8');

// 1280 x 800: classic 7 x 5, head bar 6 x 4, upright 4 x 6.5 (grid_layout.h).
const COLS = 7, ROWS = 5, COUNT = 64, SHOWN_COLS = 6, SHOWN_ROWS = 4;
const SHOWN = {classic: [COLS, ROWS], bar: [SHOWN_COLS, SHOWN_ROWS], portrait: [4, 6.5]};
// Every layout the firmware knows (grid_layout::key): the import runs on a
// panel showing each, and restores each. A new layout fails here until it
// is added to SHOWN and to the import.
const LAYOUT_KEYS = [...read('src/tiles/config/grid_layout.h')
  .match(/constexpr const char\* key\(Layout layout\) \{([\s\S]*?)\n\}/)[1].matchAll(/"([a-z_]+)"/g)].map(m => m[1]);
assert.deepEqual([...LAYOUT_KEYS].sort(), Object.keys(SHOWN).sort(), 'every layout has its shown grid here');
const importSource = read('src/web/admin/tiles/import-export.js');
const restoredKeys = JSON.parse(importSource.match(/for \(const \[layoutIndex, key\] of (\[[^\]]*\])\.entries\(\)\)/)[1]
  .replace(/'/g, '"'));
assert.deepEqual([...restoredKeys].sort(), [...LAYOUT_KEYS].sort(), 'the import restores every layout');
const SAVER = 65534;
const HALF_TYPES = [1, 2, 4, 5, 7, 8, 9, 14, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26];
const empty = () => ({type: 0, title: '', icon_name: '', bg_color: 0, col: 0, row: 0, span_w: 1, span_h: 1});
const tile = (type, title, col, row, w = 1, h = 1, extra = {}) =>
  ({...empty(), type, title, col, row, span_w: w, span_h: h, ...extra});

function makePanel(active = 'bar') {
  const panel = {
    folders: [{id: 0, parent_id: 0, name: 'Home', icon_name: 'home'}],
    grids: {0: Array.from({length: COUNT}, empty), [SAVER]: Array.from({length: COUNT}, empty)},
    places: {bar: {}, portrait: {}, classic_parked: {}, pages: []},
    clock: {classic: [500, 350, 48, 28], bar: [500, 350, 48, 28], portrait: [500, 350, 48, 28]},
    parked: [false, false, false], parkedSettings: 0, nextFolder: 1, nextView: 1, tilePosts: [], layoutPosts: []
  };
  panel.put = (folder, index, data) => {
    panel.grids[folder][index] = folder === SAVER ? data : {...data, view_id: panel.nextView++};
    return panel.grids[folder][index];
  };
  panel.createFolder = (parent, name, icon) => {
    const id = panel.nextFolder++;
    panel.folders.push({id, parent_id: parent, name, icon_name: icon});
    panel.grids[id] = Array.from({length: COUNT}, empty);
    panel.put(id, 0, tile(8, '', 0, 0, 1, 1, {icon_name: 'arrow-left'}));
    return id;
  };
  const deleteFolder = id => {
    for (const child of panel.folders.filter(folder => folder.parent_id === id)) deleteFolder(child.id);
    panel.folders = panel.folders.filter(folder => folder.id !== id);
    delete panel.grids[id];
    for (const key of ['bar', 'portrait']) delete panel.places[key][id];
    panel.places.pages = panel.places.pages.map(page => (page === id ? 0 : page));
  };
  // The stored grid's tiles: the screensaver's keyed by slot + 1.
  // Home with the Settings tile parked off it, as handleSaveLayouts loads it.
  const views = folder => (panel.grids[folder] || []).map((entry, index) =>
    (entry.type === 0 ? 0 : folder === SAVER ? index + 1 : entry.view_id))
    .concat(folder === 0 && panel.parkedSettings ? [panel.parkedSettings] : []);
  const overlaps = (grid, self, rect) => grid.some((other, i) => i !== self && other.type !== 0 && !other.layout_hidden &&
    !(rect.col + rect.span_w <= other.col || other.col + other.span_w <= rect.col ||
      rect.row + rect.span_h <= other.row || other.row + other.span_h <= rect.row));
  const supported = (type, r) => [r.col, r.row, r.span_w, r.span_h].every(v => Number.isInteger(v * 2)) &&
    r.span_w >= 1 && r.span_h >= 0.5 && r.col + r.span_w <= COLS && r.row + r.span_h <= ROWS &&
    (r.span_h >= 1 || (HALF_TYPES.includes(type) && r.span_h === 0.5));
  const answer = (status, body) => ({ok: status === 200, status, json: async () => body});

  panel.fetch = async (url, options = {}) => {
    const u = new URL(url, 'http://panel');
    if (u.pathname === '/api/folders') return answer(200, panel.folders.map(folder => ({...folder})));
    if (u.pathname === '/api/screensaver') {
      return answer(200, {success: true, screen_width: 1280, screen_height: 800, grid_cols: COLS, grid_rows: ROWS});
    }
    if (u.pathname === '/api/layouts' && options.method !== 'POST') {
      return answer(200, {success: true, active, stored: active, places: structuredClone(panel.places),
                          clock: structuredClone(panel.clock), settings: {parked: [...panel.parked],
                          ...(panel.parkedSettings ? {tile: {type: 7, view_id: panel.parkedSettings}} : {})}});
    }
    if (u.pathname === '/api/tiles' && options.method !== 'POST') {
      assert.equal(u.searchParams.get('layout'), 'classic', 'export and import read the classic places');
      const grid = panel.grids[Number(u.searchParams.get('folder'))];
      return grid ? answer(200, grid.map(entry => ({...entry}))) : answer(404, {error: 'Folder not found'});
    }
    if (u.pathname === '/api/tiles') {
      // handleSaveTiles with layout=classic: the stored grid's bounds whatever
      // layout is shown (setPlaceGrid).
      const fd = options.body;
      const folder = Number(fd.get('folder')), index = Number(fd.get('index')), type = Number(fd.get('type'));
      const grid = panel.grids[folder];
      panel.tilePosts.push({folder, index, type, layout: fd.get('layout')});
      if (!grid) return answer(404, {success: false, error: 'Folder not found'});
      const rect = {col: Number(fd.get('col')), row: Number(fd.get('row')),
                    span_w: Number(fd.get('span_w')), span_h: Number(fd.get('span_h'))};
      if (type !== 0 && !supported(type, rect)) return answer(400, {success: false, error: 'Unsupported tile size'});
      if (type !== 0 && overlaps(grid, index, rect)) return answer(409, {success: false, error: 'Tile overlaps'});
      const previous = grid[index];
      if (previous.type === 4 && type !== 4) deleteFolder(previous.navigate_target);
      if (type === 0) {
        grid[index] = empty();
        return answer(200, {success: true});
      }
      const saved = {...empty(), type, title: fd.get('title') || '', icon_name: fd.get('icon_name') || '', ...rect};
      if (type === 4) {
        const target = Number(fd.get('navigate_target'));
        saved.navigate_target = target > 0 && panel.grids[target] ? target : panel.createFolder(folder, saved.title, saved.icon_name);
      }
      // A system tile keeps its ID, a new tile gets one.
      if (previous.type === type && (type === 7 || type === 8)) saved.view_id = previous.view_id;
      grid[index] = folder === SAVER || saved.view_id ? saved : {...saved, view_id: panel.nextView++};
      return answer(200, {success: true, ...(type === 4 ? {navigate_target: saved.navigate_target} : {})});
    }
    if (u.pathname === '/api/layouts') {
      // handleSaveLayouts: everything checked before anything is written.
      const request = JSON.parse(options.body);
      panel.layoutPosts.push(request);
      const key = request.layout;
      if (!['classic', 'bar', 'portrait'].includes(key)) return answer(400, {success: false, error: 'Unknown layout'});
      if (request.pages && key === 'classic') return answer(400, {success: false, error: 'Pages need the head bar'});
      const pages = request.pages ? request.pages.filter(Boolean) : panel.places.pages.filter(Boolean);
      for (const page of pages) {
        if (!panel.folders.some(folder => folder.id === page && folder.parent_id === 0)) {
          return answer(400, {success: false, error: 'Not a Home folder'});
        }
      }
      for (const [folderKey, places] of Object.entries(request.folders || {})) {
        const folder = Number(folderKey);
        const present = views(folder);
        for (const [view, place] of Object.entries(places)) {
          if (!present.includes(Number(view)) || place.length !== 4) return answer(409, {success: false, error: 'Invalid place'});
        }
        if (key === 'classic') continue;
        for (const entry of panel.grids[folder] || []) {
          if (entry.type !== 4 || (folder === 0 && pages.includes(entry.navigate_target))) continue;
          if (!places[entry.view_id]) return answer(409, {success: false, error: 'Folder without place'});
        }
      }
      if (request.pages) panel.places.pages = request.pages.slice();
      for (const [folderKey, places] of Object.entries(request.folders || {})) {
        const folder = Number(folderKey);
        const kept = panel.places[key][folder] = {};
        for (const view of views(folder)) {
          const entry = panel.grids[folder][folder === SAVER ? view - 1 : panel.grids[folder].findIndex(t => t.view_id === view)];
          if (view && places[view] && entry?.type !== 8) kept[view] = places[view];
        }
      }
      if (request.clock) panel.clock[key] = request.clock.slice();
      if (typeof request.settings_parked === 'boolean') panel.parked[['classic', 'bar', 'portrait'].indexOf(key)] = request.settings_parked;
      return answer(200, {success: true});
    }
    throw new Error('unexpected request ' + url);
  };
  return panel;
}

// The page code of a panel showing a layout: the layout helpers, the
// screensaver's first row (navigation.js) and the import/export module.
function loadPage(panel, active = 'bar') {
  const notifications = [];
  const downloads = [];
  class PageURL extends URL {
    static createObjectURL(blob) { downloads.push(blob.parts.join('')); return 'blob:export'; }
    static revokeObjectURL() {}
  }
  const navigation = read('src/web/admin/folders/navigation.js');
  const firstRowSource = navigation.slice(navigation.indexOf('  function isScreensaverTileTab('),
                                          navigation.indexOf('  function restoreCurrentTileSelectionUi('));
  assert.ok(firstRowSource.includes('function firstAllowedGridRow('), 'navigation.js still has the first row helper');
  const context = vm.createContext({
    console: {error() {}, warn() {}, log() {}},
    URL: PageURL, FormData, Promise, setTimeout: () => 0, structuredClone,
    Blob: class { constructor(parts) { this.parts = parts; } },
    HEAD_BAR: active !== 'classic', GRID_SHOWN_COLS: SHOWN[active][0], GRID_SHOWN_ROWS: SHOWN[active][1],
    ACTIVE_LAYOUT: active,
    GRID_COLS: COLS, GRID_ROWS: ROWS, TILES_PER_GRID: COUNT, MEDIA_TILE_TYPE: 15, MEDIA_TILE_MIN_SPAN: 2, MEDIA_TILE_MAX_SPAN: 4,
    LAYOUTS: Object.fromEntries(LAYOUT_KEYS.map(key => [key, {screenW: 1280, screenH: 800, available: true,
                                                             cols: SHOWN[key][0], rows: SHOWN[key][1]}])),
    SCREENSAVER_FOLDER_ID: SAVER, tabByFolder: {0: 'folder0', [SAVER]: 'screensaver'}, currentTileTab: 'folder0',
    normalizeIconName: value => String(value || '').replace(/^mdi[:-]/, '').toLowerCase(),
    ssClamp: (value, min, max) => Math.max(min, Math.min(max, Number(value))),
    devicePreviewKind: () => 'lock', getClimateLayoutPayload: value => value || 0,
    cameraPictureOptionsOf: () => ({picture: false, fit: 'cover', every: 0, transparency: 0, head: 'none', tap: 'popup'}),
    t: key => key,
    showNotification: (text, ok = true) => notifications.push({text, ok}),
    localStorage: {removeItem() {}}, location: {reload() {}},
    document: {getElementById: () => null, querySelector: () => null,
               createElement: () => ({click() {}, remove() {}}), body: {appendChild() {}}},
    fetch: panel.fetch,
    // The access settings save (access.js): hiding parks the tile with its ID.
    readSettingsAccessState: () => ({}),
    saveSettingsAccess: async (pin, target, snapshot, state) => {
      const index = panel.grids[0].findIndex(entry => entry.type === 7);
      if (!state.tileHidden || index < 0) return false;
      panel.parkedSettings = panel.grids[0][index].view_id;
      panel.grids[0][index] = empty();
      return {ok: true};
    }
  });
  vm.runInContext(firstRowSource, context, {filename: 'navigation.js'});
  for (const file of ['src/web/admin/tiles/layout.js', 'src/web/admin/tiles/import-export.js']) {
    vm.runInContext(read(file), context, {filename: file});
  }
  return {context, notifications, downloads};
}

// The V2: a sensor and the Settings tile in the classic grid's seventh
// column, a page folder and a folder tile on Home, a camera in the seventh
// column of the page, the screensaver on the classic grid's two bottom rows,
// and its own places in both bar layouts.
const source = makePanel();
const home = source.grids[0];
const settings = source.put(0, 0, tile(7, '', 6, 4, 1, 1, {icon_name: 'cog'}));
const test = source.put(0, 1, tile(1, 'Test', 6, 1, 1, 0.5));
// Pushed into the shown grid, "Test" met this one: the conflict on the V2.
const neighbour = source.put(0, 4, tile(1, 'Neighbour', 5, 1, 1, 0.5));
const rooms = source.put(0, 2, tile(4, 'Rooms', 0, 0, 1, 1, {icon_name: 'sofa', navigate_target: source.createFolder(0, 'Rooms', 'sofa')}));
const cams = source.put(0, 3, tile(4, 'Cams', 1, 0, 1, 1, {icon_name: 'cctv', navigate_target: source.createFolder(0, 'Cams', 'cctv')}));
const camera = source.put(rooms.navigate_target, 1, tile(18, 'Door', 6, 0));
const inside = source.put(cams.navigate_target, 1, tile(1, 'Inside', 3, 2));
source.put(SAVER, 0, tile(1, 'Saver low', 6, 4));
source.put(SAVER, 3, tile(5, 'Saver switch', 0, 3));
source.places = {
  bar: {
    0: {[test.view_id]: [5, 0, 1, 0.5], [neighbour.view_id]: [4, 0, 1, 0.5], [cams.view_id]: [0, 0, 1, 1],
        [settings.view_id]: [5, 3, 1, 1]},
    [rooms.navigate_target]: {[camera.view_id]: [2, 1, 2, 2]},
    [cams.navigate_target]: {[inside.view_id]: [1, 1, 1, 1]},
    [SAVER]: {1: [5, 3, 1, 1], 4: [0, 3, 1, 1]}
  },
  portrait: {
    0: {[test.view_id]: [3, 0, 1, 0.5], [neighbour.view_id]: [2, 0, 1, 0.5], [cams.view_id]: [0, 0, 1, 1],
        [settings.view_id]: [3, 5, 1, 1]},
    [rooms.navigate_target]: {[camera.view_id]: [0, 1, 2, 2]},
    [cams.navigate_target]: {[inside.view_id]: [0, 0, 1, 1]},
    [SAVER]: {1: [3, 5.5, 1, 1], 4: [0, 4.5, 1, 1]}
  },
  classic_parked: {},
  pages: [rooms.navigate_target]
};
source.clock = {classic: [500, 350, 48, 28], bar: [120, 640, 64, 30], portrait: [300, 900, 56, 26]};
source.parked = [false, false, true];
assert.equal(home.filter(entry => entry.type).length, 5);

// 0. Every key GET /api/layouts reports (handleGetLayouts) and every key of
// the places (tile_layouts::append_json) has its place in the backup; a new
// one fails here until the backup carries it.
const handlers = read('src/web/server/handlers/web_admin_tiles.cpp');
const getLayouts = cppFunctionDefinitions(handlers).find(f => f.name === 'WebAdminServer::handleGetLayouts');
assert.ok(getLayouts, 'handleGetLayouts exists');
const reported = new Set([...getLayouts.body.matchAll(/\\"([a-z_]+)\\":/g)].map(m => m[1]));
const carried = {
  success: 'not data', error: 'not data', active: 'this start only; the backup keeps the stored choice', stored: 'layouts.active',
  classic: 'grids and screensaver.grid', places: 'layouts.places', clock: 'layouts.clock',
  settings: 'layouts.settings_parked', parked: 'layouts.settings_parked', hidden: 'settings_tile.hidden',
  tile: 'layouts.parked_settings_view',
  view_id: 'layouts.parked_settings_view', type: 'settings_tile', title: 'settings_tile', icon_name: 'settings_tile',
  bg_color: 'settings_tile', col: 'settings_tile', row: 'settings_tile', span_w: 'settings_tile', span_h: 'settings_tile'
};
for (const key of reported) assert.ok(carried[key], `GET /api/layouts reports "${key}": decide how a backup carries it`);
for (const key of ['stored', 'classic', 'places', 'clock', 'settings', 'tile']) assert.ok(reported.has(key), key);
const appendJson = cppFunctionDefinitions(read('src/tiles/config/tile_layouts.cpp')).find(f => f.name === 'append_json');
assert.ok(appendJson && /\\"pages\\"/.test(appendJson.body), 'the places carry the pages');
const placeKeys = LAYOUT_KEYS.map(key => (key === 'classic' ? 'classic_parked' : key)).concat('pages');
for (const key of placeKeys) assert.ok(importSource.includes(key === 'bar' || key === 'portrait' ? `'${key}'` : key), `the import reads ${key}`);

// 1. The export carries every layout.
const exporter = loadPage(source);
await exporter.context.exportTilesConfig();
assert.deepEqual(exporter.notifications.at(-1), {text: 'exportCreated', ok: true}, JSON.stringify(exporter.notifications));
const backup = JSON.parse(exporter.downloads[0]);
assert.deepEqual(backup.layouts.places, source.places, 'the bar and upright places with the pages');
assert.deepEqual(backup.layouts.clock, source.clock, 'the screensaver clock per layout');
assert.deepEqual(backup.layouts.settings_parked, source.parked, 'where Settings is parked per layout');
assert.deepEqual(Object.keys(backup.layouts.places).sort(), [...placeKeys].sort(), 'every key of the places');

// The 8-inch showing each layout in turn: its own tiles, folder and places.
for (const active of LAYOUT_KEYS) {
  const target = makePanel(active);
  target.put(0, 63, tile(7, '', 6, 4, 1, 1, {icon_name: 'cog'}));
  target.put(0, 0, tile(1, 'Old', 0, 0));
  const old = target.put(0, 1, tile(4, 'Old', 1, 0, 1, 1, {navigate_target: target.createFolder(0, 'Old', 'folder')}));
  target.places.pages = [old.navigate_target];
  target.nextView = 500;

  // 2. The import takes the classic places as exported, on the stored grid,
  // whatever layout the panel shows.
  const importer = loadPage(target, active);
  await importer.context.importTilesPayload(JSON.parse(JSON.stringify(backup)));
  assert.deepEqual(importer.notifications.at(-1), {text: 'importComplete', ok: true}, active + ' ' + JSON.stringify(importer.notifications));
  assert.ok(target.tilePosts.every(post => post.layout === 'classic'), 'every tile is written with its classic place');
  const find = (folder, title) => target.grids[folder].find(entry => entry.type && entry.title === title);
  const roomsId = find(0, 'Rooms').navigate_target, camsId = find(0, 'Cams').navigate_target;
  const settingsTile = target.grids[0].find(entry => entry.type === 7);
  assert.deepEqual([settingsTile.col, settingsTile.row], [6, 4], 'Settings in the seventh column');
  assert.deepEqual([find(0, 'Test').col, find(0, 'Test').row, find(0, 'Test').span_h], [6, 1, 0.5], 'Test in the seventh column');
  assert.deepEqual([find(roomsId, 'Door').col, find(roomsId, 'Door').row], [6, 0], 'the camera in the seventh column');
  assert.deepEqual([target.grids[SAVER][0].col, target.grids[SAVER][0].row], [6, 4], 'the screensaver keeps its bottom row');
  assert.deepEqual([target.grids[SAVER][3].col, target.grids[SAVER][3].row], [0, 3]);

  // 3. Every layout came along, on the imported tiles' new IDs.
  const view = (folder, title) => find(folder, title).view_id;
  assert.deepEqual(target.places.pages, [roomsId], 'the page is the imported folder');
  for (const key of ['bar', 'portrait']) {
    const expected = {
      0: {[view(0, 'Test')]: source.places[key][0][test.view_id], [view(0, 'Neighbour')]: source.places[key][0][neighbour.view_id],
          [view(0, 'Cams')]: source.places[key][0][cams.view_id],
          [settingsTile.view_id]: source.places[key][0][settings.view_id]},
      [roomsId]: {[view(roomsId, 'Door')]: source.places[key][rooms.navigate_target][camera.view_id]},
      [camsId]: {[view(camsId, 'Inside')]: source.places[key][cams.navigate_target][inside.view_id]},
      [SAVER]: source.places[key][SAVER]
    };
    for (const [folder, places] of Object.entries(expected)) {
      assert.deepEqual(target.places[key][folder], places, `${active}: ${key} places of folder ${folder}`);
    }
  }
  assert.deepEqual(target.clock, source.clock, 'the screensaver clock in every layout');
  assert.deepEqual(target.parked, source.parked, 'Settings parked as exported');

  // 4. A backup of the 8-inch now gives the same layouts back.
  const again = loadPage(target, active);
  await again.context.exportTilesConfig();
  const second = JSON.parse(again.downloads[0]);
  assert.deepEqual(second.layouts.clock, backup.layouts.clock);
  assert.deepEqual(second.layouts.settings_parked, backup.layouts.settings_parked);
  assert.equal(Object.keys(second.layouts.places.bar).length, Object.keys(backup.layouts.places.bar).length);
}

// 5. Settings parked off Home: its kept places follow the target's parked
// tile; one a shown tile took meanwhile (upright here) yields.
{
  const parkedSource = makePanel();
  const shown = parkedSource.put(0, 0, tile(1, 'Shown', 0, 0));
  parkedSource.parkedSettings = 77;
  parkedSource.places.bar = {0: {[shown.view_id]: [0, 0, 1, 1], 77: [1, 3, 1, 1]}};
  parkedSource.places.portrait = {0: {[shown.view_id]: [0, 0, 1, 1], 77: [0, 0, 1, 1]}};
  parkedSource.parked = [true, true, false];
  const page = loadPage(parkedSource);
  await page.context.exportTilesConfig();
  const parkedBackup = JSON.parse(page.downloads[0]);
  assert.equal(parkedBackup.layouts.parked_settings_view, 77);
  const parkedTarget = makePanel();
  const targetSettings = parkedTarget.put(0, 63, tile(7, '', 6, 4, 1, 1, {icon_name: 'cog'}));
  const parkedImport = loadPage(parkedTarget);
  await parkedImport.context.importTilesPayload(parkedBackup);
  assert.deepEqual(parkedImport.notifications.at(-1), {text: 'importComplete', ok: true}, JSON.stringify(parkedImport.notifications));
  assert.equal(parkedTarget.parkedSettings, targetSettings.view_id, 'the target parked its own Settings tile');
  const shownView = parkedTarget.grids[0].find(entry => entry.title === 'Shown').view_id;
  assert.deepEqual(parkedTarget.places.bar[0], {[shownView]: [0, 0, 1, 1], [targetSettings.view_id]: [1, 3, 1, 1]});
  assert.deepEqual(parkedTarget.places.portrait[0], {[shownView]: [0, 0, 1, 1]}, 'the taken cell stays with the shown tile');
  assert.deepEqual(parkedTarget.parked, [true, true, false]);
}

console.log('ok - head bar import keeps the classic places and brings every layout back');
