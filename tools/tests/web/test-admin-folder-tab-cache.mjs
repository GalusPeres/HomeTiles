// Two Web Admin regressions around folder tabs:
// - Returning to a folder tab threw "TILES_PER_GRID is not defined" because the
//   page never declared the constant, so the tile selection was not restored.
// - The folder tab cache lived in sessionStorage, so every new browser tab
//   asked the device for every folder again. It now lives in localStorage,
//   shared by all tabs of the same device boot.
import assert from 'node:assert/strict';
import vm from 'node:vm';

import {readRepoFile} from '../../lib/admin-source.mjs';

const read = file => readRepoFile(file).replace(/\r\n?/g, '\n');
const pageScripts = read('src/web/server/render/web_admin_scripts.cpp');
assert.match(pageScripts,
  /html \+= " {2}const TILES_PER_GRID = " \+\n {10}String\(static_cast<unsigned>\(TILES_PER_GRID\)\) \+ ";\\n";/,
  'the page declares TILES_PER_GRID next to GRID_COLS/GRID_ROWS');

const sessionCache = read('src/web/admin/folders/session-cache.js');
assert.doesNotMatch(sessionCache, /sessionStorage/, 'folder tab cache no longer per browser tab');
assert.ok(read('src/web/assets/admin.js').includes(
  "const raw = localStorage.getItem(folderTabSessionIndexKey());"), 'generated admin.js carries the shared cache');

function memoryStorage() {
  const items = new Map();
  return {
    get length() { return items.size; },
    key: index => Array.from(items.keys())[index] ?? null,
    getItem: key => (items.has(key) ? items.get(key) : null),
    setItem: (key, value) => { items.set(key, String(value)); },
    removeItem: key => { items.delete(key); },
  };
}

// One browser tab: only what the page itself declares (TILES_PER_GRID as
// asserted above), plus a tiny DOM.
function browserTab(storage, token, {markedIndex = null} = {}) {
  const tiles = {};
  const context = {
    localStorage: storage,
    ADMIN_WEB_SESSION_TOKEN: token,
    APP_LOCALE: 'de',
    GRID_COLS: 7,
    GRID_ROWS: 5,
    tabByFolder: {0: 'home', 3: 'folder3'},
    selectedTileByTab: {},
    document: {
      querySelector: () => (markedIndex === null ? null
        : {dataset: {type: '3', index: String(markedIndex)}}),
      querySelectorAll: () => [],
      getElementById: id => tiles[id] || null,
    },
  };
  vm.createContext(context);
  vm.runInContext('const TILES_PER_GRID = 35;\n' + sessionCache +
    '\nthis.api = {getRememberedTileIndex, rememberFolderTabSessionFragment, readFolderTabSessionFragment, prepareFolderTabSessionCache};',
    context);
  return {context, tiles, api: context.api};
}

{
  const {api} = browserTab(memoryStorage(), 1, {markedIndex: 4});
  assert.equal(api.getRememberedTileIndex('folder3'), 4, 'a marked tile is restored');
}
{
  const {context, tiles, api} = browserTab(memoryStorage(), 1);
  context.selectedTileByTab.folder3 = 6;
  tiles['folder3-tile-6'] = {dataset: {type: '1'}};
  assert.equal(api.getRememberedTileIndex('folder3'), 6, 'a remembered selection is restored');
}
{
  const storage = memoryStorage();
  const first = browserTab(storage, 77);
  first.api.rememberFolderTabSessionFragment({folder_id: 3, tab_id: 'folder3', tab_html: '<div>3</div>'});
  const second = browserTab(storage, 77);
  assert.deepEqual({...second.api.readFolderTabSessionFragment(3)},
    {folder_id: 3, tab_id: 'folder3', tab_html: '<div>3</div>'}, 'a second browser tab reuses the folder tab');
  const afterReboot = browserTab(storage, 78);
  assert.equal(afterReboot.api.readFolderTabSessionFragment(3), null, 'a new device boot never reuses it');
  afterReboot.api.prepareFolderTabSessionCache();
  assert.equal(storage.length, 1, 'the old boot entries are removed (only the new empty index is left)');
}

console.log('Folder tabs: TILES_PER_GRID declared by the page, cache shared by browser tabs of one device boot.');
