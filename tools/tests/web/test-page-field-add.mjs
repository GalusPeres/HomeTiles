// A free page field's "+" (#100, user 2026-10-22) with the delivered Web
// Admin code and a simulated device: a click asks the panel for a new Home
// folder that is the page there at once (/api/pages new=<slot>), its tile
// stands in the field, its folder gets its tab and the editor picks it to
// name it. A second click on the same field sends nothing, a full Home says
// so and changes nothing. The panel makes the folder by the folder tile's own
// save path and needs no free cell on Home's screen.
import assert from 'node:assert/strict';
import {readAdminDeliverySource, readRepoFile, inlineScriptSafe} from '../../lib/admin-source.mjs';
import {runDomHarness} from '../../lib/headless-dom.mjs';

const TILES = 12;
const head = (folder, button) => `<div class="head-bar-preview" aria-hidden="true"><span class="head-disc"><i class="mdi mdi-home"></i></span>` +
  `<span class="head-title">${folder}</span><span class="head-time">12:00</span>` +
  `<span class="head-button ${button}"><i class="mdi mdi-${button ? 'cog' : 'window-close'}"></i></span></div>`;
const html = `<!doctype html><html><head><style>
${readRepoFile('src/web/assets/admin.css')}
:root { --grid-cols:6; --grid-rows:4; --preview-cell-w:100px; --preview-cell-h:80px; --preview-gap:10px; --preview-pad:10px; }
</style></head><body>
<div class="wrapper with-page-fields"><div id="tab-tiles-folder0" class="tile-tab tab-content active" data-folder-id="0" data-tab-id="folder0"><div class="tile-editor"><div class="tile-editor-main">
<div class="tile-grid-scroll"><div class="tile-grid head-bar-grid" id="folder0Grid">${head('Home', 'head-gear')}
${Array.from({length: TILES}, (_, i) => `<div class="tile empty" id="folder0-tile-${i}" data-index="${i}" data-type="0" draggable="true" onclick="selectTile(parseInt(this.dataset.index), 'folder0')"></div>`).join('')}
</div></div>
<div id="pageSlots" class="page-fields" data-pages=""><div class="page-fields-label">Pages</div>${
  Array.from({length: 5}, (_, i) => `<div class="page-field" data-slot="${i}"></div>`).join('')}<div id="settingsHiddenSlot" class="tile-grid settings-hidden-slot" data-in-grid="1"><div id="settingsHiddenTile" class="tile settings-hidden-tile empty" data-hidden="0"></div></div></div>
</div></div></div></div>
<div id="tab-network"></div>
<div class="tab-nav"><div id="folderTabs">
<button class="tab-btn folder-tab-btn" type="button" data-folder-id="1" data-folder-parent="0" data-tab-id="folder1" data-folder-name="Lights" data-folder-icon="lightbulb"><span class="tab-disc"><i class="mdi mdi-lightbulb"></i></span><span class="tab-label">Lights</span></button>
</div></div>
<div id="folder0Settings" class="tile-settings"><div class="tile-specific-settings">
<select id="folder0_tile_type"><option value="0">Empty</option><option value="4">Folder</option><option value="7">Settings</option></select>
<textarea id="folder0_tile_title"></textarea><input id="folder0_tile_icon"><input type="color" id="folder0_tile_color">
${['col','row','span_w','span_h'].map(name => `<input id="folder0_tile_${name}" type="number" value="1">`).join('')}
</div></div><pre id="result">running</pre><script>
const nativeListen = document.addEventListener.bind(document);
document.addEventListener = (name, ...args) => { if (name !== 'DOMContentLoaded') nativeListen(name, ...args); };
const APP_I18N = {}, CLIMATE_I18N = {}, BINARY_SENSOR_I18N = {};
const GRID_COLS = 7, GRID_ROWS = 5, GRID_SHOWN_COLS = 6, GRID_SHOWN_ROWS = 4, HEAD_BAR = true;
const TILES_PER_GRID = ${TILES}, ADMIN_WEB_SESSION_TOKEN = 'test', ACTIVE_LAYOUT = 'bar';
const TILE_TYPE_REGISTRY = {
  4: {label:'Folder',css:'navigate',fields:'navigate',preview:'none',load:'loadNavigateFields',reset:'resetNavigateFields'},
  7: {label:'Settings',css:'navigate',fields:'settings_access',preview:'none',locked:true,reset:'resetNavigateFields'}
};
const TILE_TABS = [], TAB_BY_FOLDER = {}, FOLDER_BY_TAB = {}, SCREENSAVER_FOLDER_ID = 65535;
const SCREENSAVER_TILE_DEFAULT_OPACITY = 0, SCREENSAVER_TILE_DEFAULT_COLOR = '#000000';
const MEDIA_TILE_MIN_SPAN = 2, MEDIA_TILE_MAX_SPAN = 3, MEDIA_TILE_TYPE = 15;
// The simulated device: Home's screen full (6 x 4 cells taken by one tile
// each would be too many for the entries, so one big tile covers it), a
// folder and the Settings tile.
const empty = () => ({type:0});
const device = Array.from({length: ${TILES}}, empty);
device[0] = {type:4,title:'Lights',icon_name:'lightbulb',navigate_target:1,col:0,row:0,span_w:1,span_h:1};
device[1] = {type:2,title:'Wall',icon_name:'lightbulb',col:1,row:0,span_w:5,span_h:4};
device[2] = {type:7,title:'Settings',icon_name:'cog',col:0,row:3,span_w:1,span_h:1};
let pages = [];
let nextFolder = 20;
const log = [];
window.addEventListener('error', event => log.push('ERR ' + event.message));
const notes = [];
const wait = ms => new Promise(resolve => setTimeout(resolve, ms));
window.fetch = async (url, options) => {
  if (options?.method === 'POST') {
    const body = Object.fromEntries(new URLSearchParams(options.body));
    log.push(url + ' ' + JSON.stringify(body));
    await wait(20);
    if (url !== '/api/pages') return {ok:true, status:200, json:async()=>({success:true})};
    const wanted = body.pages ? body.pages.split(',').map(Number) : [];
    const slot = Number(body.new);
    while (wanted.length <= slot) wanted.push(0);
    if (wanted[slot]) return {ok:false, status:400, json:async()=>({success:false,error:'Not a free slot'})};
    const index = device.findIndex(tile => !tile.type);
    if (index < 0) return {ok:false, status:409, json:async()=>({success:false,error:'Folder full'})};
    const folder = nextFolder++;
    device[index] = {type:4,title:'Folder ' + folder,icon_name:'',navigate_target:folder,col:index % 6,row:0,span_w:1,span_h:1,
      layout_hidden:true,view_id:100 + folder};
    wanted[slot] = folder;
    pages = wanted;
    return {ok:true, status:200, json:async()=>({success:true,places:{},folder,index,title:'Folder ' + folder})};
  }
  const tabMatch = String(url).match(/^\\/api\\/folders\\/tab\\?folder_id=(\\d+)/);
  if (tabMatch) {
    const id = Number(tabMatch[1]);
    log.push('TAB ' + id);
    return {ok:true, status:200, json:async()=>({success:true, tab_id:'folder' + id,
      button_html:'<button class="tab-btn folder-tab-btn" type="button" data-folder-id="' + id + '" data-folder-parent="0" data-tab-id="folder' + id +
        '" data-folder-name="Folder ' + id + '" data-folder-icon=""><span class="tab-disc"><i class="mdi mdi-folder"></i></span><span class="tab-label">Folder ' + id + '</span></button>',
      tab_html:'<div id="tab-tiles-folder' + id + '" class="tile-tab tab-content" data-folder-id="' + id + '" data-tab-id="folder' + id +
        '"><div class="tile-grid head-bar-grid" id="folder' + id + 'Grid">' + ${JSON.stringify(head('Folder', ''))} + '</div></div>'})};
  }
  if (String(url).startsWith('/api/tiles?folder=0')) {
    await wait(10);
    return {ok:true, status:200, json:async()=>JSON.parse(JSON.stringify(device))};
  }
  return {ok:true, status:200, json:async()=>({})};
};
${inlineScriptSafe(readAdminDeliverySource())}
showNotification = (message, ok = true) => { notes.push((ok ? 'OK ' : 'NO ') + message); };
(async () => { try {
  const check = (value, message) => { if (!value) throw Error(message + '\\n' + log.join('\\n') + '\\n' + notes.join('\\n')); };
  const grid = getTileGrid('folder0');
  const box = el => el.getBoundingClientRect();
  const field = slot => grid.querySelector(':scope > .page-field[data-slot="' + slot + '"]');
  const tileOf = folder => document.getElementById('folder0-tile-' + homeFolderTileIndex(folder));
  // Where a field lies (a taken one is hidden under its tile).
  const metric = name => parseFloat(getComputedStyle(grid).getPropertyValue(name)) || 0;
  const fieldCorner = slot => {
    const place = pageFieldPlaces(grid).pages[slot];
    return {x: box(grid).left + grid.clientLeft + parseFloat(getComputedStyle(grid).paddingLeft) + place.col * (metric('--preview-cell-w') + metric('--preview-gap')),
      y: box(grid).top + grid.clientTop + parseFloat(getComputedStyle(grid).paddingTop) + place.row * (metric('--preview-cell-h') + metric('--preview-gap'))};
  };
  const inField = (el, slot) => el && el.classList.contains('page-field-tile') &&
    Math.abs(box(el).left - fieldCorner(slot).x) < 1 && Math.abs(box(el).top - fieldCorner(slot).y) < 1;
  const dots = tab => [...document.querySelectorAll('#tab-tiles-' + tab + ' .head-dots > i')].map(dot => dot.classList.contains('on') ? 'O' : 'o').join('');
  const posts = () => log.filter(line => line.startsWith('/api/pages'));
  const settle = () => wait(400);
  // Home's tab is known by its markup, as on the panel's page (a new
  // folder's tab runs initTileTabs again).
  initTileTabs();
  tileDataLoadedTabs.add('folder0');
  tilesData.folder0 = JSON.parse(JSON.stringify(device));
  tilesData.folder0.forEach((tile, i) => renderTileFromData('folder0', i, tile, {}));
  layoutTiles('folder0', tilesData.folder0);
  enableTileDrag('folder0');
  enableSettingsHiddenSlot();
  enablePageFields();

  // Every free field shows its "+" and takes a click; the frame stays a
  // tile's size.
  for (let slot = 0; slot < 5; ++slot) {
    const f = field(slot), plus = f.querySelector(':scope > .page-field-add');
    check(f.classList.contains('can-add') && plus && plus.classList.contains('mdi-plus') &&
          getComputedStyle(plus).display !== 'none' && getComputedStyle(f).pointerEvents === 'auto' &&
          getComputedStyle(f).display === 'flex', 'Field ' + slot + ' shows its +');
  }
  check(Math.abs(box(field(0)).width - box(tileOf(1)).width) < 0.5, 'The field keeps a tile\\'s size');

  // A click makes the page there, though Home's screen has no free cell.
  field(2).dispatchEvent(new MouseEvent('click', {bubbles:true}));
  await settle();
  check(posts().length === 1 && posts()[0] === '/api/pages {"pages":"","new":"2"}', 'One request for slot 2: ' + posts().join());
  check(pages.join() === '0,0,20' && document.getElementById('pageSlots').dataset.pages === '0,0,20', 'The page stands in slot 2: ' + pages.join());
  check(inField(tileOf(20), 2) && field(2).hidden, 'Its tile stands in its field, the frame under it');
  check(dots('folder0') === 'Oo', 'Home shows its dots: ' + dots('folder0'));
  check(document.querySelector('.folder-tab-btn[data-folder-id="20"] .tab-label')?.textContent === 'Folder 20' &&
        document.getElementById('tab-tiles-folder20'), 'Its folder has its tab');
  check(currentTileTab === 'folder0' && currentTileIndex === homeFolderTileIndex(20) && tileOf(20).classList.contains('active'),
        'The editor picks its tile to name it: ' + currentTileIndex);
  check(notes.at(-1)?.startsWith('OK '), 'It says so: ' + notes.at(-1));

  // Two quick clicks on one field: one page.
  field(4).dispatchEvent(new MouseEvent('click', {bubbles:true}));
  field(4).dispatchEvent(new MouseEvent('click', {bubbles:true}));
  await settle();
  check(posts().length === 2 && posts()[1].includes('"new":"4"') && pages.join() === '0,0,20,0,21',
        'A second click finds the field taken: ' + posts().join() + ' / ' + pages.join());
  check(inField(tileOf(21), 4) && dots('folder0') === 'Ooo', 'The second page in its field: ' + dots('folder0'));

  // A full Home says so and changes nothing.
  for (let i = 0; i < ${TILES}; ++i) if (!device[i].type) device[i] = {type:2,title:'X',col:0,row:0,span_w:1,span_h:1};
  field(0).dispatchEvent(new MouseEvent('click', {bubbles:true}));
  await settle();
  check(posts().length === 3 && document.getElementById('pageSlots').dataset.pages === '0,0,20,0,21' && !field(0).hidden &&
        notes.at(-1)?.startsWith('NO '), 'A full Home changes nothing: ' + notes.at(-1));
  check(!log.some(line => line.startsWith('ERR')), 'No script error');
  document.body.dataset.result = 'pass';
} catch (error) {
  document.body.dataset.result = 'fail'; document.getElementById('result').textContent = error.stack;
}})();
</script></body></html>`;

try {
  runDomHarness({label: 'Page field add', html, tmpPrefix: 'hometiles-page-field-add-', extraArgs: ['--virtual-time-budget=8000']});
} catch (error) {
  throw Error(error.message.match(/<pre id="result">([\s\S]*?)<\/pre>/)?.[1] || error.message.slice(0, 1500));
}

// The panel: a free slot only, the first free entry of Home (else "Folder
// full"), the folder by the folder tile's save path, named like an unnamed
// folder, its tile a hidden page tile before the grid is saved, the folder
// gone again when that save fails; the answer names folder, entry and title.
const tiles = readRepoFile('src/web/server/handlers/web_admin_tiles.cpp').replace(/\r\n/g, '\n');
const save = tiles.slice(tiles.indexOf('void WebAdminServer::handleSavePages() {'), tiles.indexOf('void WebAdminServer::handleGetFolderTab() {'));
assert.match(save, /if \(new_slot < 0 \|\| new_slot >= static_cast<long>\(wanted\.size\(\)\) \|\| wanted\[new_slot\]\) \{\s*sendJsonError\(server, 400, "Not a free slot"\);/);
assert.ok(save.indexOf('"Not a free slot"') < save.indexOf('checkPageSlots(wanted)'), 'The slot is checked with the others');
assert.match(save, /if \(home->tiles\[i\]\.type == TILE_EMPTY\) new_index = static_cast<int>\(i\);/);
assert.match(save, /if \(new_index < 0\) \{\s*sendJsonError\(server, 409, "Folder full"\);/);
assert.match(save, /tile\.type = TILE_FOLDER;\s*tile\.view_id = 0;[\s\S]*?tile\.span_w = 1;\s*tile\.span_h = 1;\s*tile\.layout_hidden = true;/);
assert.match(save, /apply_navigate_fields_from_request\(server, tile, TileConfig::rootFolderId\(\), tileConfig, error\)/);
assert.match(save, /tile\.title = hometiles_title::normalize\(\(String\(tr\.folder_prefix\) \+ String\(new_folder\)\)\.c_str\(\)\)\.c_str\(\);/);
assert.match(save, /if \(!tileConfig\.updateFolder\(new_folder, tile\.title, tile\.icon_name\) \|\|\s*!tileConfig\.saveFolderGrid\(TileConfig::rootFolderId\(\), \*home\)\) \{\s*tileConfig\.deleteFolder\(new_folder\);/);
assert.match(save, /wanted\[new_slot\] = new_folder;/);
// Before the folders leaving the slots take their cells: the saved grid is
// Home as it is plus the new tile.
assert.ok(save.indexOf('wanted[new_slot] = new_folder;') < save.indexOf('std::vector<Spot> spots;'), 'Made before the spots');
assert.match(save, /if \(new_folder\) \{\s*json \+= ",\\"folder\\":";[\s\S]*?",\\"index\\":";[\s\S]*?appendJsonEscaped\(json, home->tiles\[new_index\]\.title\);/);
// No navigate_target in the request: the save path makes a folder.
const navigate = readRepoFile('src/types/navigate/web_handler.cpp');
assert.match(navigate, /if \(target_id == 0 \|\| !tileConfig\.folderExists\(target_id\)\) \{\s*uint16_t new_id = 0;\s*if \(!tileConfig\.createFolder\(folder_id, tile\.title, tile\.icon_name, new_id\)\)/);
// A page's hidden tile gets no place in the active bar layout; the classic
// one gives a new tile the first free place, else its storage.
const config = readRepoFile('src/tiles/config/tile_config.cpp').replace(/\r\n/g, '\n');
assert.ok(config.includes('if (!tile.layout_hidden && tile.type != TILE_BACK) {\n      tile_layouts::set(active, folder_id, tile.view_id,'));
assert.match(config, /const bool free = own \|\| findFreeClassicPlace\(occupied, tile, place\);\s*tile_layouts::set_classic_hidden\(folder_id, tile\.view_id, !free\);/);
console.log('Page field add: a + on a free field makes a page there, its tab, the editor picks it; one per field, a full Home says so');
