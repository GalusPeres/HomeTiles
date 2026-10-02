// Issue #70: importing an export from a device with a hidden Settings tile
// into one where it stands in the bottom-right cell stopped halfway: Home
// was already emptied when the folder tile "More" was refused (409 Tile
// overlaps). The real export and import code runs here against a fake
// panel with the firmware's placement rules: the Settings tile follows the
// export (hidden stays hidden, shown comes back at its exported place),
// Back tiles take their exported place, the whole layout is checked before
// anything is written, empty cells are not written, and a refused tile is
// named in the user's language.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8');
const exported = JSON.parse(read('build/issue-70/export.json'));

const COLS = 4, ROWS = 4, COUNT = COLS * ROWS;
const HALF_TYPES = [1, 2, 4, 5, 7, 8, 9, 14, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26];
const empty = () => ({type: 0, title: '', icon_name: '', bg_color: 0, col: 0, row: 0, span_w: 1, span_h: 1});

// A panel with the placement rules of handleSaveTiles and TileConfig.
function makePanel({settings = {col: 3, row: 3}, extraHome = true} = {}) {
  const panel = {folders: [{id: 0, parent_id: 0, name: 'Home', icon_name: 'home'}], grids: {0: []},
                 posts: [], access: [], settingsHidden: !settings, nextId: 1, failTitle: null};
  for (let i = 0; i < COUNT; i++) panel.grids[0].push(empty());
  panel.grids[65535] = Array.from({length: COUNT}, empty);
  if (settings) panel.grids[0][15] = {...empty(), type: 7, title: '', icon_name: 'cog', ...settings};
  const createFolder = (parent, name, icon) => {
    const id = panel.nextId++;
    panel.folders.push({id, parent_id: parent, name, icon_name: icon});
    panel.grids[id] = Array.from({length: COUNT}, empty);
    panel.grids[id][0] = {...empty(), type: 8, icon_name: 'arrow-left'};
    return id;
  };
  const deleteFolder = id => {
    for (const child of panel.folders.filter(folder => folder.parent_id === id && folder.id !== id)) deleteFolder(child.id);
    panel.folders = panel.folders.filter(folder => folder.id !== id);
    delete panel.grids[id];
  };
  if (extraHome) {
    panel.grids[0][0] = {...empty(), type: 1, title: 'Old sensor', col: 0, row: 0};
    const old = createFolder(0, 'Old', 'folder');
    panel.grids[0][1] = {...empty(), type: 4, title: 'Old', icon_name: 'folder', col: 1, row: 0, navigate_target: old};
    panel.grids[old][1] = {...empty(), type: 1, title: 'Old inside', col: 1, row: 0};
  }
  const overlaps = (grid, self, rect) => grid.some((tile, i) => i !== self && tile.type !== 0 &&
    !(rect.col + rect.span_w <= tile.col || tile.col + tile.span_w <= rect.col ||
      rect.row + rect.span_h <= tile.row || tile.row + tile.span_h <= rect.row));
  const supported = (type, r) => [r.col, r.row, r.span_w, r.span_h].every(v => Number.isInteger(v * 2)) &&
    r.span_w >= 1 && r.span_h >= 0.5 && r.col + r.span_w <= COLS && r.row + r.span_h <= ROWS &&
    (r.span_h >= 1 || (HALF_TYPES.includes(type) && r.span_h === 0.5));
  const answer = (status, body) => ({ok: status === 200, status, json: async () => body});
  panel.fetch = async (url, options = {}) => {
    const u = new URL(url, 'http://panel');
    if (u.pathname === '/api/folders') return answer(200, panel.folders.map(folder => ({...folder})));
    if (u.pathname === '/api/tiles' && options.method !== 'POST') {
      const grid = panel.grids[Number(u.searchParams.get('folder'))];
      return grid ? answer(200, grid.map(tile => ({...tile}))) : answer(404, {error: 'Folder not found'});
    }
    if (u.pathname === '/api/tiles') {
      const fd = options.body;
      const folder = Number(fd.get('folder')), index = Number(fd.get('index')), type = Number(fd.get('type'));
      const grid = panel.grids[folder];
      panel.posts.push({folder, index, type, title: fd.get('title')});
      if (!grid) return answer(404, {success: false, error: 'Folder not found'});
      if (fd.get('title') === panel.failTitle) return answer(500, {success: false, error: 'Tile apply failed'});
      if (type === 7 && folder !== 0) return answer(400, {success: false, error: 'Settings tile only allowed in Home'});
      if (type === 8 && folder === 0) return answer(400, {success: false, error: 'Back tile only allowed in folders'});
      if ((type === 7 || type === 8) && grid.some((tile, i) => i !== index && tile.type === type)) {
        return answer(409, {success: false, error: 'Settings tile already exists'});
      }
      const rect = {col: Number(fd.get('col')), row: Number(fd.get('row')),
                    span_w: Number(fd.get('span_w')), span_h: Number(fd.get('span_h'))};
      if (type !== 0 && !supported(type, rect)) return answer(400, {success: false, error: 'Unsupported tile size'});
      if (type !== 0 && overlaps(grid, index, rect)) return answer(409, {success: false, error: 'Tile overlaps'});
      const previous = grid[index];
      if (previous.type === 4 && type !== 4) deleteFolder(previous.navigate_target);
      const tile = {...empty(), type, title: fd.get('title') || '', icon_name: fd.get('icon_name') || '', ...(type ? rect : {})};
      if (type === 4) {
        const target = Number(fd.get('navigate_target'));
        tile.navigate_target = target > 0 && panel.grids[target] ? target : createFolder(folder, tile.title, tile.icon_name);
      }
      grid[index] = tile;
      return answer(200, {success: true, ...(type === 4 ? {navigate_target: tile.navigate_target} : {})});
    }
    if (u.pathname === '/api/screensaver') return answer(200, {success: true});
    throw new Error('unexpected request ' + url);
  };
  // The access settings save (access.js): hide parks the tile, show puts
  // the snapshot at the target like ensureSettingsTile.
  panel.saveSettingsAccess = async (pin, target, snapshot, state) => {
    panel.access.push({hidden: state.tileHidden, target, snapshot});
    const home = panel.grids[0];
    if (state.tileHidden) {
      const index = home.findIndex(tile => tile.type === 7);
      if (index >= 0) home[index] = empty();
      panel.settingsHidden = true;
      return {ok: true};
    }
    const rect = {col: target.col, row: target.row, span_w: Number(snapshot.span_w || 1), span_h: Number(snapshot.span_h || 1)};
    const index = home.findIndex(tile => tile.type === 0);
    if (overlaps(home, -1, rect)) return false;
    home[index] = {...empty(), type: 7, title: snapshot.title || '', icon_name: snapshot.icon_name || 'cog', ...rect};
    panel.settingsHidden = false;
    return {ok: true};
  };
  return panel;
}

// The page code: layout helpers and the import/export module, as bundled.
function loadImport(panel, parkedTile = null) {
  const notifications = [];
  const context = vm.createContext({
    console: {error() {}, warn() {}, log() {}},
    URL, FormData, Blob: class {}, Promise, setTimeout: () => 0,
    GRID_COLS: COLS, GRID_ROWS: ROWS, MEDIA_TILE_TYPE: 15, MEDIA_TILE_MIN_SPAN: 2, MEDIA_TILE_MAX_SPAN: 4,
    SCREENSAVER_FOLDER_ID: 65535, tabByFolder: {0: 'folder0'}, currentTileTab: 'folder0',
    firstAllowedGridRow: tab => (tab === 'screensaver' ? ROWS - 2 : 0),
    normalizeIconName: value => String(value || '').replace(/^mdi[:-]/, '').toLowerCase(),
    ssClamp: (value, min, max) => Math.max(min, Math.min(max, Number(value))),
    devicePreviewKind: () => 'lock', getClimateLayoutPayload: value => value || 0,
    t: key => ({importConflict: 'CONFLICT {tile} | {folder}', importStopped: 'STOPPED {tile} | {folder}',
                importScreensaver: 'Screensaver'}[key] || key),
    showNotification: (text, ok = true) => notifications.push({text, ok}),
    localStorage: {removeItem() {}}, location: {reload() {}},
    document: {
      getElementById: id => (id === 'settingsHiddenTile' ? parkedTile : null),
      querySelector: selector => ({7: {textContent: 'Settings'}, 8: {textContent: 'Back'}}[/value="(\d+)"/.exec(selector)?.[1]] || null)
    },
    fetch: panel.fetch,
    saveSettingsAccess: panel.saveSettingsAccess,
    readSettingsAccessState: () => ({pinEnabled: true, pinConfigured: true, tileHidden: panel.settingsHidden,
                                     swipeEnabled: true, revealEdge: '0'})
  });
  for (const file of ['src/web/admin/tiles/layout.js', 'src/web/admin/tiles/import-export.js']) {
    vm.runInContext(read(file), context, {filename: file});
  }
  return {context, notifications};
}

const visible = grid => grid.filter(tile => tile.type !== 0)
  .map(tile => `${tile.type}@${tile.col},${tile.row} ${tile.span_w}x${tile.span_h} ${tile.title}`).sort();
const exportedVisible = grid => visible(grid.map(tile => ({...empty(), ...tile})));

// 1. The reporter's export into a device with the Settings tile in the
// bottom-right cell, an old tile and an old folder with content.
{
  const panel = makePanel();
  const {context, notifications} = loadImport(panel);
  await context.importTilesPayload(structuredClone(exported));
  assert.deepEqual(notifications.at(-1), {text: 'importComplete', ok: true}, JSON.stringify(notifications));
  assert.equal(panel.access.length, 1, 'the Settings tile is hidden like in the export');
  assert.equal(panel.access[0].hidden, true);
  assert.equal(panel.settingsHidden, true);
  assert.deepEqual(visible(panel.grids[0]).map(line => line.replace(/ \d+$/, '')),
                   exportedVisible(exported.grids['0']).map(line => line.replace(/ \d+$/, '')), 'Home as exported');
  // Every exported folder exists with its exported tiles, the old one is gone.
  const byName = name => panel.folders.find(folder => folder.name === name);
  assert.equal(byName('Old'), undefined, 'the replaced folder is removed');
  for (const folder of exported.folders.filter(folder => folder.id !== 0)) {
    const target = byName(folder.name);
    assert.ok(target, folder.name + ' is created');
    assert.equal(target.parent_id, folder.parent_id === 0 ? 0 : byName(exported.folders.find(f => f.id === folder.parent_id).name).id);
    assert.deepEqual(visible(panel.grids[target.id]).map(line => line.replace(/ \d+$/, '')),
                     exportedVisible(exported.grids[String(folder.id)]), folder.name + ' as exported');
  }
  // Only the two old Home tiles were cleared; no empty cell was written.
  const clears = panel.posts.filter(post => post.type === 0);
  assert.equal(clears.length, 2, 'only occupied cells are cleared: ' + JSON.stringify(clears));
  const imported = Object.values(exported.grids).flat().filter(tile => tile.type !== 0 && tile.type !== 8).length;
  const movedBack = Object.entries(exported.grids).filter(([id]) => id !== '0').length;
  assert.equal(panel.posts.length, clears.length + imported + movedBack, 'one write per imported tile');
}

// 2. A new export carries the hidden flag and the parked tile; the import
// parks that tile.
{
  const parked = {dataset: {hidden: '1', title: 'Setup', icon: 'tools', bgColor: '0', col: '2', row: '3', spanW: '1', spanH: '0.5'}};
  const source = loadImport(makePanel(), parked);
  const state = source.context.exportSettingsTileState();
  assert.deepEqual({...state}, {hidden: true, title: 'Setup', icon_name: 'tools', bg_color: 0, col: 2, row: 3, span_w: 1, span_h: 0.5});
  const panel = makePanel();
  const {context, notifications} = loadImport(panel);
  await context.importTilesPayload({...structuredClone(exported), settings_tile: state});
  assert.equal(notifications.at(-1).text, 'importComplete');
  assert.equal(panel.access[0].hidden, true);
  assert.equal(panel.access[0].snapshot.title, 'Setup', 'the exported parked tile is kept');
  assert.equal(panel.access[0].snapshot.row, 3);
}

// 3. Shown in the export, hidden on the device: it comes back at the
// exported place after Home is written.
{
  const payload = structuredClone(exported);
  payload.grids['0'][15] = {...empty(), type: 7, title: '', icon_name: 'cog', col: 2, row: 3, span_w: 1, span_h: 1};
  payload.settings_tile = {hidden: false};
  const panel = makePanel({settings: null});
  const {context, notifications} = loadImport(panel);
  await context.importTilesPayload(payload);
  assert.equal(notifications.at(-1).text, 'importComplete', JSON.stringify(notifications));
  assert.equal(panel.access.length, 1);
  assert.equal(panel.access[0].hidden, false);
  assert.deepEqual({...panel.access[0].target}, {col: 2, row: 3});
  assert.ok(visible(panel.grids[0]).includes('7@2,3 1x1 '), visible(panel.grids[0]).join('\n'));
}

// 4. Shown on both: the device's Settings tile moves to the exported place;
// a Back tile moved in the export keeps its place in the new folder.
{
  const payload = structuredClone(exported);
  payload.grids['0'][15] = {...empty(), type: 7, title: '', icon_name: 'cog', col: 2, row: 2, span_w: 1, span_h: 1};
  const kantoor = payload.grids['5'];
  kantoor[kantoor.findIndex(tile => tile.type === 8)] = {...empty(), type: 8, icon_name: 'arrow-left', col: 3, row: 3, span_w: 1, span_h: 1};
  const panel = makePanel();
  const {context, notifications} = loadImport(panel);
  await context.importTilesPayload(payload);
  assert.equal(notifications.at(-1).text, 'importComplete', JSON.stringify(notifications));
  assert.equal(panel.access.length, 0, 'visibility unchanged');
  assert.ok(visible(panel.grids[0]).includes('7@2,2 1x1 '), visible(panel.grids[0]).join('\n'));
  const target = panel.folders.find(folder => folder.name === 'Kantoor');
  assert.ok(visible(panel.grids[target.id]).includes('8@3,3 1x1 '), visible(panel.grids[target.id]).join('\n'));
}

// 5. A layout that cannot be placed stops before anything is written.
{
  const payload = structuredClone(exported);
  payload.grids['2'][15] = {...empty(), type: 1, title: 'Stacked', col: 0, row: 1, span_w: 1, span_h: 1};
  const panel = makePanel();
  const before = structuredClone(panel.grids);
  const {context, notifications} = loadImport(panel);
  await context.importTilesPayload(payload);
  assert.deepEqual(notifications.at(-1), {text: 'CONFLICT Stacked | Beneden', ok: false});
  assert.equal(panel.posts.length, 0, 'nothing written');
  assert.equal(panel.access.length, 0, 'Settings untouched');
  assert.deepEqual(panel.grids, before);
}

// 6. A tile the panel still refuses is named with its folder.
{
  const panel = makePanel();
  panel.failTitle = 'Bowser';
  const {context, notifications} = loadImport(panel);
  await context.importTilesPayload(structuredClone(exported));
  assert.deepEqual(notifications.at(-1), {text: 'STOPPED Bowser | Logan', ok: false});
}

// The messages exist in every language with both placeholders, and the
// page receives them.
const header = read('src/core/i18n/i18n.h');
const fields = [...header.slice(header.indexOf('struct Strings')).matchAll(/const char\* (\w+);/g)].map(m => m[1]);
const table = read('src/core/i18n/i18n.cpp');
for (const name of ['kStringsDe', 'kStringsEn', 'kStringsFr']) {
  const start = table.indexOf('static const Strings ' + name);
  const block = table.slice(start, table.indexOf('\n};', start));
  const values = [...block.matchAll(/^\s+(".*?"(?:\s*\n\s*".*?")*|nullptr),?\s*(?:\/\/.*)?$/gm)].map(m => m[1]);
  for (const field of ['js_import_conflict', 'js_import_stopped']) {
    const value = values[fields.indexOf(field)];
    assert.ok(value.includes('{tile}') && value.includes('{folder}'), `${name} ${field}: ${value}`);
  }
  assert.ok(values[fields.indexOf('js_import_screensaver')].length > 2, name + ' screensaver name');
}
const scripts = read('src/web/server/render/web_admin_scripts.cpp');
for (const key of ['importConflict', 'importStopped', 'importScreensaver']) {
  assert.ok(scripts.includes(`appendJsEntry("${key}"`), key);
}
assert.doesNotMatch(read('src/web/admin/tiles/import-export.js'), /fehlgeschlagen/, 'no German text in the page code');

console.log('Import: Settings tile follows the export (hidden, shown, moved), Back tiles keep their place, ' +
            'the layout is checked before writing, no empty cell is written, refused tiles are named');
