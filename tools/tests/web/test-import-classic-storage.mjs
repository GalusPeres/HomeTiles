// A tile in the classic storage (the layout window's) keeps its old classic
// place in the tile data, and another tile may stand there by now. The export
// marks such a tile (layout_hidden), and the import puts it back into the
// storage instead of stopping on the overlap: with its spot beside the screen
// when the export came from the same screen. The real export and import code
// runs against a fake panel with the firmware's rules for these tiles.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8');

const COLS = 4, ROWS = 4, COUNT = COLS * ROWS;
const empty = () => ({type: 0, title: '', icon_name: '', bg_color: 0, col: 0, row: 0, span_w: 1, span_h: 1});

function makePanel() {
  const panel = {grids: {0: Array.from({length: COUNT}, empty)}, posts: [], layoutPosts: []};
  panel.grids[0][15] = {...empty(), type: 7, icon_name: 'cog', col: 3, row: 3};
  panel.grids[0][0] = {...empty(), type: 1, title: 'Old', col: 0, row: 0};
  const answer = (status, body) => ({ok: status === 200, status, json: async () => body});
  // handleSaveTiles with layout=classic: a storage tile takes no room.
  const overlaps = (grid, self, rect) => grid.some((tile, i) => i !== self && tile.type !== 0 && !tile.layout_hidden &&
    !(rect.col + rect.span_w <= tile.col || tile.col + tile.span_w <= rect.col ||
      rect.row + rect.span_h <= tile.row || tile.row + tile.span_h <= rect.row));
  panel.fetch = async (url, options = {}) => {
    const u = new URL(url, 'http://panel');
    if (u.pathname === '/api/folders') return answer(200, [{id: 0, parent_id: 0, name: 'Home', icon_name: 'home'}]);
    if (u.pathname === '/api/tiles' && options.method !== 'POST') {
      return answer(200, panel.grids[Number(u.searchParams.get('folder'))].map(tile => ({...tile})));
    }
    if (u.pathname === '/api/tiles') {
      const fd = options.body;
      const index = Number(fd.get('index')), type = Number(fd.get('type'));
      const grid = panel.grids[Number(fd.get('folder'))];
      const post = {index, type, title: fd.get('title'), layout: fd.get('layout'),
                    hidden: fd.get('layout_hidden'), spot: fd.get('layout_spot')};
      panel.posts.push(post);
      const rect = {col: Number(fd.get('col')), row: Number(fd.get('row')),
                    span_w: Number(fd.get('span_w')), span_h: Number(fd.get('span_h'))};
      const hidden = type !== 0 && post.hidden === '1';
      if (type !== 0 && (rect.col + rect.span_w > COLS || rect.row + rect.span_h > ROWS)) {
        return answer(400, {success: false, error: 'Invalid layout'});
      }
      if (type !== 0 && !hidden && overlaps(grid, index, rect)) return answer(409, {success: false, error: 'Tile overlaps'});
      grid[index] = {...empty(), type, title: post.title || '', ...(type ? rect : {}), ...(hidden ? {layout_hidden: true} : {})};
      return answer(200, {success: true});
    }
    if (u.pathname === '/api/layouts') {
      panel.layoutPosts.push(JSON.parse(options.body));
      return answer(200, {success: true});
    }
    throw new Error('unexpected request ' + url);
  };
  return panel;
}

function loadImport(panel) {
  const notifications = [];
  const context = vm.createContext({
    console: {error() {}, warn() {}, log() {}},
    URL, FormData, Blob: class {}, Promise, setTimeout: () => 0,
    GRID_COLS: COLS, GRID_ROWS: ROWS, MEDIA_TILE_TYPE: 15, MEDIA_TILE_MIN_SPAN: 2, MEDIA_TILE_MAX_SPAN: 4,
    LAYOUTS: {classic: {screenW: 800, screenH: 480}, bar: {available: true}, portrait: {available: false}},
    SCREENSAVER_FOLDER_ID: 65535, tabByFolder: {0: 'folder0'}, currentTileTab: 'folder0',
    firstAllowedGridRow: () => 0,
    normalizeIconName: value => String(value || '').replace(/^mdi[:-]/, '').toLowerCase(),
    ssClamp: (value, min, max) => Math.max(min, Math.min(max, Number(value))),
    devicePreviewKind: () => 'lock', getClimateLayoutPayload: value => value || 0,
    t: key => key,
    showNotification: (text, ok = true) => notifications.push({text, ok}),
    localStorage: {removeItem() {}}, location: {reload() {}},
    document: {getElementById: () => null, querySelector: () => null},
    fetch: panel.fetch
  });
  for (const file of ['src/web/admin/tiles/layout.js', 'src/web/admin/tiles/import-export.js']) {
    vm.runInContext(read(file), context, {filename: file});
  }
  return {context, notifications};
}

// The export: "Stored" lies in the classic storage, its old place under
// "Shown"; the Settings tile as on the panel.
const exported = (screen = {screen_width: 800, screen_height: 480}) => ({
  version: 3,
  folders: [{id: 0, parent_id: 0, name: 'Home', icon_name: 'home'}],
  grids: {'0': [
    {type: 1, title: 'Stored', view_id: 5, col: 0, row: 0, span_w: 1, span_h: 1, layout_hidden: true},
    {type: 1, title: 'Shown', view_id: 6, col: 0, row: 0, span_w: 1, span_h: 1},
    {type: 7, title: '', icon_name: 'cog', view_id: 7, col: 3, row: 3, span_w: 1, span_h: 1}
  ]},
  layouts: {...screen, active: 'classic',
            places: {bar: {}, portrait: {}, classic_parked: {'0': {'5': [4, 0, 1, 1], '9': [5, 0, 1, 1]}}}}
});

// 1. Same screen: no conflict; the stored tile goes back into the storage
// with its spot, the other one onto the screen.
{
  const panel = makePanel();
  const {context, notifications} = loadImport(panel);
  await context.importTilesPayload(exported());
  assert.deepEqual(notifications.at(-1), {text: 'importComplete', ok: true}, JSON.stringify(notifications));
  const stored = panel.posts.find(post => post.title === 'Stored');
  const shown = panel.posts.find(post => post.title === 'Shown');
  assert.equal(stored.layout, 'classic');
  assert.equal(stored.hidden, '1', 'the storage tile is marked');
  assert.equal(stored.spot, '4,0,1,1', 'with its spot beside the screen');
  assert.equal(shown.hidden, null, 'a tile on the screen is not marked');
  assert.equal(shown.spot, null);
  const home = panel.grids[0].filter(tile => tile.type !== 0);
  assert.ok(home.some(tile => tile.title === 'Shown' && tile.col === 0 && tile.row === 0 && !tile.layout_hidden));
  assert.ok(home.some(tile => tile.title === 'Stored' && tile.layout_hidden));
}

// 2. Another screen: still in the storage, without a spot (the window finds
// it one beside the screen).
{
  const panel = makePanel();
  const {context, notifications} = loadImport(panel);
  await context.importTilesPayload(exported({screen_width: 1280, screen_height: 720}));
  assert.equal(notifications.at(-1).text, 'importComplete', JSON.stringify(notifications));
  const stored = panel.posts.find(post => post.title === 'Stored');
  assert.equal(stored.hidden, '1');
  assert.equal(stored.spot, null);
}

// 3. A placeholder place outside a smaller grid is moved inside it, so the
// server's geometry check passes.
{
  const panel = makePanel();
  const {context, notifications} = loadImport(panel);
  const payload = exported();
  Object.assign(payload.grids['0'][0], {col: 6, row: 3, span_w: 2, span_h: 1});
  await context.importTilesPayload(payload);
  assert.equal(notifications.at(-1).text, 'importComplete', JSON.stringify(notifications));
  const stored = panel.grids[0].find(tile => tile.title === 'Stored');
  assert.ok(stored.col + stored.span_w <= COLS, JSON.stringify(stored));
}

// 4. The firmware: the export and the import mark the classic storage tiles,
// the import leaves them out of the overlap check and stores their spot.
{
  const source = read('src/web/server/handlers/web_admin_tiles.cpp');
  assert.match(source, /if \(loaded && classic_places\) markClassicStorage\(folder_id, grid\);/);
  assert.match(source, /if \(grid_loaded && classic_places\) markClassicStorage\(folder_id, \*grid\);/);
  assert.match(source, /tile\.layout_hidden = classic_places && tile\.type != TILE_EMPTY && server\.arg\("layout_hidden"\) == "1";/);
  assert.match(source, /if \(!tile\.layout_hidden && placementOverlaps\(\*grid, index, rect\)\)/);
  assert.match(source, /tile_layouts::set\(grid_layout::Layout::kClassic, folder_id, tile\.view_id, storage_spot\);/);
  assert.match(source, /tile_layouts::set_classic_hidden\(folder_id, tile\.view_id, tile\.layout_hidden\);/);
}

console.log('Import: classic storage tiles go back into the storage, with their spot on the same screen');
