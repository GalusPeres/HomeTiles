// A free page field (#100, user 2026-10-22: a click made a folder at once,
// mistakes guaranteed) and the editor's picks with the delivered Web Admin
// code and a simulated device. A click only picks a free field (its teal
// ring, a "+" in it alone, Type with Empty and Folder only); its "+" or
// Folder in Type asks the panel for a new Home folder that is the page there
// (/api/pages new=<slot>), named by the editor's title. Only the new tile is
// loaded and picked at once, its tab follows. A new folder from the grid
// gets "Folder N" and the folder icon like its tab. Each tab keeps its pick
// (a tile, an empty cell, a page field) through the 15 s refresh, the way to
// another tab and back, and a pick while the grid loads. Deleting the page
// frees and picks its field, the panel's word reloads without its tab. A
// full Home says so and changes nothing.
import assert from 'node:assert/strict';
import {readAdminDeliverySource, readRepoFile, inlineScriptSafe} from '../../lib/admin-source.mjs';
import {runDomHarness} from '../../lib/headless-dom.mjs';

const TILES = 12;
const head = (folder, button) => `<div class="head-bar-preview" aria-hidden="true"><span class="head-disc"><i class="mdi mdi-home"></i></span>` +
  `<span class="head-title">${folder}</span><span class="head-time">12:00</span>` +
  `<span class="head-button ${button}"><i class="mdi mdi-${button ? 'cog' : 'window-close'}"></i></span></div>`;
const editor = tab => `<div id="${tab}Settings" class="tile-settings"><div class="tile-specific-settings hidden">
<div class="tile-settings-head"><div class="tile-head-top"><h3>Tile</h3>
<select id="${tab}_tile_type"><option value="0">Empty</option><option value="1">Sensor</option><option value="4">Folder</option><option value="7" hidden>Settings</option></select></div>
<div class="tile-head-row"><textarea id="${tab}_tile_title"></textarea><input type="hidden" id="${tab}_tile_icon"></div></div>
<div class="tile-settings-body"><input type="color" id="${tab}_tile_color">
${['col', 'row', 'span_w', 'span_h'].map(name => `<input id="${tab}_tile_${name}" type="number" value="1">`).join('')}</div>
</div></div>`;
const tileElements = tab => Array.from({length: TILES}, (_, i) =>
  `<div class="tile empty" id="${tab}-tile-${i}" data-index="${i}" data-type="0" draggable="true" onclick="selectTile(parseInt(this.dataset.index), '${tab}')"></div>`).join('');
const html = `<!doctype html><html><head><style>
${readRepoFile('src/web/assets/admin.css')}
:root { --grid-cols:6; --grid-rows:4; --preview-cell-w:100px; --preview-cell-h:80px; --preview-gap:10px; --preview-pad:10px; }
</style></head><body>
<div class="wrapper with-page-fields"><div id="tab-tiles-folder0" class="tile-tab tab-content active" data-folder-id="0" data-tab-id="folder0"><div class="tile-editor"><div class="tile-editor-main">
<div class="tile-grid-scroll"><div class="tile-grid head-bar-grid" id="folder0Grid">${head('Home', 'head-gear')}
${tileElements('folder0')}
</div></div>
<div id="pageSlots" class="page-fields" data-pages=""><div class="page-fields-label">Pages</div>${
  Array.from({length: 5}, (_, i) => `<div class="page-field" data-slot="${i}"></div>`).join('')}<div id="settingsHiddenSlot" class="tile-grid settings-hidden-slot" data-in-grid="1"><div id="settingsHiddenTile" class="tile settings-hidden-tile empty" data-hidden="0"></div></div></div>
</div></div>${editor('folder0')}</div>
<div id="tab-tiles-folder1" class="tile-tab tab-content" data-folder-id="1" data-tab-id="folder1"><div class="tile-grid head-bar-grid" id="folder1Grid">${head('Lights', '')}
${tileElements('folder1')}</div>${editor('folder1')}</div></div>
<div id="tab-network" class="tab-content"></div>
<div class="tab-nav"><div id="folderTabs">
<button class="tab-btn folder-tab-btn" type="button" data-folder-id="1" data-folder-parent="0" data-tab-id="folder1" data-folder-name="Lights" data-folder-icon="lightbulb"><span class="tab-disc"><i class="mdi mdi-lightbulb"></i></span><span class="tab-label">Lights</span></button>
</div></div>
<pre id="result">running</pre><script>
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
// The simulated device: Home's screen nearly full (a folder, one big tile,
// the Settings tile; free cells at (0,1) and (0,2)), a folder in Lights.
const empty = () => ({type:0});
const device = Array.from({length: ${TILES}}, empty);
device[0] = {type:4,title:'Lights',icon_name:'lightbulb',navigate_target:1,col:0,row:0,span_w:1,span_h:1,view_id:101};
device[1] = {type:2,title:'Wall',icon_name:'lightbulb',col:1,row:0,span_w:5,span_h:4,view_id:102};
device[2] = {type:7,title:'Settings',icon_name:'cog',col:0,row:3,span_w:1,span_h:1,view_id:103};
const lights = Array.from({length: ${TILES}}, empty);
lights[0] = {type:4,title:'Lamps',icon_name:'lamp',navigate_target:5,col:0,row:0,span_w:1,span_h:1,view_id:201};
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
    if (url === '/api/tiles') {
      const index = Number(body.index), before = device[index];
      if (body.type === '0' && before?.type === 4) {
        device[index] = empty();
        pages = pages.map(id => id === before.navigate_target ? 0 : id);
        return {ok:true, status:200, json:async()=>({success:true, folder_deleted: before.navigate_target})};
      }
      if (body.type === '4' && before?.type !== 4) {
        // Like navigate/web_handler.cpp: a new folder without title or icon
        // gets "Folder N" and the folder icon.
        const folder = nextFolder++;
        const title = body.title || 'Folder ' + folder, icon = body.icon_name || 'folder';
        device[index] = {type:4,title,icon_name:icon,navigate_target:folder,col:Number(body.col),row:Number(body.row),span_w:1,span_h:1,view_id:100 + folder};
        return {ok:true, status:200, json:async()=>({success:true, view_id:100 + folder, navigate_target:folder,
          folder_pin_enabled:false, folder_pin:'', title, icon_name:icon})};
      }
      return {ok:true, status:200, json:async()=>({success:true})};
    }
    if (url !== '/api/pages') return {ok:true, status:200, json:async()=>({success:true})};
    const wanted = body.pages ? body.pages.split(',').map(Number) : [];
    const slot = Number(body.new);
    while (wanted.length <= slot) wanted.push(0);
    if (wanted[slot]) return {ok:false, status:400, json:async()=>({success:false,error:'Not a free slot'})};
    const index = device.findIndex(tile => !tile.type);
    if (index < 0) return {ok:false, status:409, json:async()=>({success:false,error:'Folder full'})};
    const folder = nextFolder++;
    const title = body.title || 'Folder ' + folder;
    device[index] = {type:4,title,icon_name:body.icon_name || 'folder',navigate_target:folder,col:index % 6,row:0,span_w:1,span_h:1,
      layout_hidden:true,view_id:100 + folder,folder_empty:true};
    wanted[slot] = folder;
    pages = wanted;
    return {ok:true, status:200, json:async()=>({success:true,places:{},folder,index,title})};
  }
  const tabMatch = String(url).match(/^\\/api\\/folders\\/tab\\?folder_id=(\\d+)/);
  if (tabMatch) {
    const id = Number(tabMatch[1]);
    log.push('TAB ' + id);
    // The panel builds a tab slowly: the new tile must not wait for it.
    await wait(500);
    log.push('TAB DONE ' + id);
    return {ok:true, status:200, json:async()=>({success:true, folder_id:id, tab_id:'folder' + id,
      button_html:'<button class="tab-btn folder-tab-btn" type="button" data-folder-id="' + id + '" data-folder-parent="0" data-tab-id="folder' + id +
        '" data-folder-name="Folder ' + id + '" data-folder-icon=""><span class="tab-disc"><i class="mdi mdi-folder"></i></span><span class="tab-label">Folder ' + id + '</span></button>',
      tab_html:'<div id="tab-tiles-folder' + id + '" class="tile-tab tab-content" data-folder-id="' + id + '" data-tab-id="folder' + id +
        '"><div class="tile-grid head-bar-grid" id="folder' + id + 'Grid">' + ${JSON.stringify(head('Folder', ''))} + '</div></div>'})};
  }
  const tilesMatch = String(url).match(/^\\/api\\/tiles\\?folder=(\\d+)(?:&index=(\\d+))?$/);
  if (tilesMatch) {
    log.push('GET ' + url);
    await wait(10);
    const grid = tilesMatch[1] === '0' ? device : lights;
    const data = tilesMatch[2] !== undefined ? grid[Number(tilesMatch[2])] : grid;
    return {ok:true, status:200, json:async()=>JSON.parse(JSON.stringify(data))};
  }
  return {ok:true, status:200, json:async()=>({})};
};
${inlineScriptSafe(readAdminDeliverySource())}
showNotification = (message, ok = true) => { notes.push((ok ? 'OK ' : 'NO ') + message); };
(async () => { try {
  const check = (value, message) => { if (!value) throw Error(message + '\\n' + log.join('\\n') + '\\n' + notes.join('\\n')); };
  // A reload is noted, not done.
  const nativeTimeout = window.setTimeout.bind(window);
  window.setTimeout = (fn, ms, ...rest) => {
    if (typeof fn === 'function' && String(fn).includes('location.reload')) { log.push('RELOAD'); return 0; }
    return nativeTimeout(fn, ms, ...rest);
  };
  const grid = getTileGrid('folder0');
  const box = el => el.getBoundingClientRect();
  const field = slot => grid.querySelector(':scope > .page-field[data-slot="' + slot + '"]');
  const plusOf = slot => field(slot).querySelector(':scope > .page-field-add');
  const shown = el => !!el && getComputedStyle(el).display !== 'none';
  const tileOf = folder => document.getElementById('folder0-tile-' + homeFolderTileIndex(folder));
  const metric = name => parseFloat(getComputedStyle(grid).getPropertyValue(name)) || 0;
  const cellCorner = (col, row) => ({
    x: box(grid).left + grid.clientLeft + parseFloat(getComputedStyle(grid).paddingLeft) + col * (metric('--preview-cell-w') + metric('--preview-gap')),
    y: box(grid).top + grid.clientTop + parseFloat(getComputedStyle(grid).paddingTop) + row * (metric('--preview-cell-h') + metric('--preview-gap'))});
  // Where a field lies (a taken one is hidden under its tile).
  const fieldCorner = slot => cellCorner(pageFieldPlaces(grid).pages[slot].col, pageFieldPlaces(grid).pages[slot].row);
  const inField = (el, slot) => el && el.classList.contains('page-field-tile') &&
    Math.abs(box(el).left - fieldCorner(slot).x) < 1 && Math.abs(box(el).top - fieldCorner(slot).y) < 1;
  const dots = tab => [...document.querySelectorAll('#tab-tiles-' + tab + ' .head-dots > i')].map(dot => dot.classList.contains('on') ? 'O' : 'o').join('');
  const posts = () => log.filter(line => line.startsWith('/api/pages'));
  const specific = document.querySelector('#folder0Settings .tile-specific-settings');
  const typeSelect = document.getElementById('folder0_tile_type');
  const offered = () => [...typeSelect.options].filter(option => !option.hidden).map(option => option.value).join();
  const click = (el, detail = 1) => el.dispatchEvent(new MouseEvent('click', {bubbles:true, detail}));
  const clickCell = (col, row) => {
    const point = {bubbles:true, clientX: cellCorner(col, row).x + 20, clientY: cellCorner(col, row).y + 20};
    grid.dispatchEvent(new MouseEvent('pointermove', point));
    grid.dispatchEvent(new MouseEvent('click', point));
  };
  const settle = () => wait(400);
  // Home's tab is known by its markup, as on the panel's page (a new
  // folder's tab runs initTileTabs again).
  initTileTabs();
  tileDataLoadedTabs.add('folder0');
  tilesData.folder0 = JSON.parse(JSON.stringify(device));
  tilesData.folder0.forEach((tile, i) => renderTileFromData('folder0', i, tile, {}));
  layoutTiles('folder0', tilesData.folder0);
  enableTileDrag('folder0');
  enableFreeSlotHover('folder0');
  enableSettingsHiddenSlot();
  enablePageFields();

  // Free fields: a thin frame, no "+", a click picks them.
  for (let slot = 0; slot < 5; ++slot) {
    check(field(slot).classList.contains('can-add') && getComputedStyle(field(slot)).pointerEvents === 'auto' &&
          !shown(plusOf(slot)) && !field(slot).classList.contains('marked'), 'Field ' + slot + ' is free without a +');
  }
  check(offered() === '0,1,4', 'The editor offers every type before: ' + offered());

  // Each tab keeps its pick through the 15 s refresh and the way to another
  // tab and back (user 2026-10-22: it jumped to the top left tile).
  const away = async () => {
    await switchTab('tab-tiles-folder1');
    await settle();
    check(currentTileTab === 'folder1', 'Lights picks its own tile: ' + currentTileTab);
    await switchTab('tab-tiles-folder0');
    await settle();
  };
  selectTile(0, 'folder0');
  await away();
  check(currentTileTab === 'folder0' && currentTileIndex === 0, 'A tile stays picked: ' + currentTileTab + ' ' + currentTileIndex);
  clickCell(0, 2);
  const emptyIndex = currentTileIndex;
  const emptyAt = () => {
    const el = document.getElementById('folder0-tile-' + emptyIndex);
    return el ? el.dataset.col + ',' + el.dataset.row + (el.classList.contains('active') ? ' picked' : '') : '';
  };
  check(emptyIndex >= 0 && !Number(getTilesData('folder0')[emptyIndex]?.type) && emptyAt() === '0,2 picked',
        'An empty cell is picked at its spot: ' + emptyIndex + ' ' + emptyAt());
  await loadSensorValues(false, false);
  check(currentTileIndex === emptyIndex && emptyAt() === '0,2 picked', 'The refresh keeps the empty cell: ' + currentTileIndex + ' ' + emptyAt());
  await away();
  check(currentTileTab === 'folder0' && currentTileIndex === emptyIndex && emptyAt() === '0,2 picked',
        'The empty cell stays picked on the way back: ' + currentTileIndex + ' ' + emptyAt());
  click(field(2));
  await loadSensorValues(false, false);
  check(field(2).classList.contains('marked') && currentTileIndex === -1, 'The refresh keeps the picked field: ' + currentTileIndex);
  await away();
  check(field(2).classList.contains('marked') && currentTileTab === 'folder0' && currentTileIndex === -1,
        'The picked field stays on the way back: ' + currentTileTab + ' ' + currentTileIndex);
  // A pick while the grid loads stays.
  tileDataLoadedTabs.delete('folder0');
  const switching = switchTab('tab-tiles-folder0');
  click(field(4));
  await switching;
  await settle();
  check(field(4).classList.contains('marked') && !field(2).classList.contains('marked') && currentTileIndex === -1,
        'A pick while the grid loads stays: ' + currentTileIndex);
  check(!posts().length, 'Picking makes nothing');

  // A click picks the field: its ring and "+", the editor with Empty and
  // Folder; nothing is made.
  click(field(2));
  await settle();
  check(!posts().length, 'A click makes nothing: ' + posts().join());
  check(field(2).classList.contains('marked') && shown(plusOf(2)) && getComputedStyle(field(2)).borderTopWidth === '3px' &&
        [0, 1, 3, 4].every(slot => !shown(plusOf(slot))), 'Only the picked field has its ring and +');
  check(!specific.classList.contains('hidden') && specific.classList.contains('page-field-marked') && offered() === '0,4' &&
        typeSelect.value === '0' && currentTileIndex === -1, 'The editor offers Empty and Folder: ' + offered());
  check(getComputedStyle(specific.querySelector('.tile-settings-body')).display === 'none' &&
        shown(document.getElementById('folder0_tile_title')), 'Type, title and icon only');

  // A double click on another field picks it and makes nothing.
  click(field(3));
  click(plusOf(3), 2);
  await settle();
  check(!posts().length && field(3).classList.contains('marked') && !field(2).classList.contains('marked'),
        'A double click only picks: ' + posts().join());

  // Its "+" makes the page, named by the editor's title; only the new tile is
  // loaded and picked at once, its tab comes after.
  document.getElementById('folder0_tile_title').value = 'Kitchen';
  const before = log.length;
  click(plusOf(3));
  await wait(5);
  check(field(3).classList.contains('busy'), 'The + is busy while the panel makes the page');
  await wait(200);
  check(posts().length === 1 && posts()[0] === '/api/pages {"pages":"","new":"3","title":"Kitchen"}', 'One request for slot 3: ' + posts().join());
  check(pages.join() === '0,0,0,20' && inField(tileOf(20), 3) && field(3).hidden, 'The page stands in its field: ' + pages.join());
  check(currentTileIndex === homeFolderTileIndex(20) && tileOf(20).classList.contains('active') && !log.includes('TAB DONE 20'),
        'The new tile is picked before its tab arrived: ' + currentTileIndex);
  const gets = log.slice(before).filter(line => line.startsWith('GET '));
  check(gets.length === 1 && gets[0] === 'GET /api/tiles?folder=0&index=' + homeFolderTileIndex(20), 'Only the new tile is loaded: ' + gets.join());
  await wait(700);
  check(document.querySelector('.folder-tab-btn[data-folder-id="20"]') && document.getElementById('tab-tiles-folder20'), 'Its folder has its tab');
  check(dots('folder0') === 'Oo', 'Home shows its dots: ' + dots('folder0'));
  check(!specific.classList.contains('page-field-marked') && !document.querySelector('.page-field.marked') && offered() === '0,1,4' &&
        currentTileIndex === homeFolderTileIndex(20), 'Every type again, the new tile stays picked: ' + offered());
  check(getTilesData('folder0')[homeFolderTileIndex(20)].title === 'Kitchen', 'Named as typed');

  // Folder in Type makes the page of the picked field too.
  click(field(1));
  typeSelect.value = '4';
  typeSelect.dispatchEvent(new Event('change', {bubbles:true}));
  await wait(900);
  check(posts().length === 2 && posts()[1] === '/api/pages {"pages":"0,0,0,20","new":"1"}' && pages.join() === '0,21,0,20' &&
        inField(tileOf(21), 1), 'Folder in Type makes the page: ' + posts().join() + ' / ' + pages.join());
  check(getTilesData('folder0')[homeFolderTileIndex(21)].title === 'Folder 21' &&
        getTilesData('folder0')[homeFolderTileIndex(21)].icon_name === 'folder', 'Named and shown like its tab');

  // A new folder from the grid gets "Folder N" and the folder icon too,
  // shown in the editor at once.
  clickCell(0, 1);
  const cellIndex = currentTileIndex;
  typeSelect.value = '4';
  typeSelect.dispatchEvent(new Event('change', {bubbles:true}));
  await wait(900);
  check(log.some(line => line.startsWith('/api/tiles') && line.includes('"type":"4"') && line.includes('"index":"' + cellIndex + '"')),
        'The grid folder is saved');
  check(document.getElementById('folder0_tile_title').value === 'Folder 22' && document.getElementById('folder0_tile_icon').value === 'folder' &&
        getTilesData('folder0')[cellIndex].title === 'Folder 22' && getTilesData('folder0')[cellIndex].icon_name === 'folder',
        'The grid folder has title and icon: ' + document.getElementById('folder0_tile_title').value);

  // Deleting a page frees and picks its field at once; the panel's word
  // reloads without its tab, the kept tab markup goes.
  check(readFolderTabSessionFragment(20), 'The tab markup of the new folder was kept');
  selectTile(homeFolderTileIndex(20), 'folder0');
  const pageIndex = currentTileIndex;
  resetTile('folder0');
  check(!document.getElementById('folder0-tile-' + pageIndex).classList.contains('page-field-tile') && !field(3).hidden &&
        field(3).classList.contains('marked') && currentTileIndex === -1, 'The field of the deleted page is free and picked');
  await wait(800);
  check(log.some(line => line.startsWith('/api/tiles') && line.includes('"index":"' + pageIndex + '"') && line.includes('"type":"0"')),
        'The deletion is saved');
  check(log.includes('RELOAD') && !readFolderTabSessionFragment(20), 'The word of the panel reloads without the tab');

  // A full Home says so and changes nothing.
  for (let i = 0; i < TILES_PER_GRID; ++i) if (!device[i].type) device[i] = {type:2,title:'X',col:0,row:0,span_w:1,span_h:1};
  click(field(4));
  click(plusOf(4));
  await settle();
  check(posts().length === 3 && !pages.includes(23) && !field(4).hidden && field(4).classList.contains('marked') &&
        !field(4).classList.contains('busy') && notes.at(-1)?.startsWith('NO '), 'A full Home changes nothing: ' + notes.at(-1));
  check(!log.some(line => line.startsWith('ERR')), 'No script error');
  document.body.dataset.result = 'pass';
} catch (error) {
  document.body.dataset.result = 'fail'; document.getElementById('result').textContent = error.stack;
}})();
</script></body></html>`;

try {
  runDomHarness({label: 'Page field add', html, tmpPrefix: 'hometiles-page-field-add-', extraArgs: ['--virtual-time-budget=20000']});
} catch (error) {
  throw Error(error.message.match(/<pre id="result">([\s\S]*?)<\/pre>/)?.[1] || error.message.slice(0, 1500));
}

// The panel: a free slot only, the first free entry of Home (else "Folder
// full"), the folder by the folder tile's save path (which names a new one
// "Folder N" with the folder icon), its tile a hidden page tile before the
// grid is saved, the folder gone again when that save fails; the answer
// names folder, entry and title.
const tiles = readRepoFile('src/web/server/handlers/web_admin_tiles.cpp').replace(/\r\n/g, '\n');
const save = tiles.slice(tiles.indexOf('void WebAdminServer::handleSavePages() {'), tiles.indexOf('void WebAdminServer::handleGetFolderTab() {'));
assert.match(save, /if \(new_slot < 0 \|\| new_slot >= static_cast<long>\(wanted\.size\(\)\) \|\| wanted\[new_slot\]\) \{\s*sendJsonError\(server, 400, "Not a free slot"\);/);
assert.ok(save.indexOf('"Not a free slot"') < save.indexOf('checkPageSlots(wanted)'), 'The slot is checked with the others');
assert.match(save, /if \(home->tiles\[i\]\.type == TILE_EMPTY\) new_index = static_cast<int>\(i\);/);
assert.match(save, /if \(new_index < 0\) \{\s*sendJsonError\(server, 409, "Folder full"\);/);
assert.match(save, /tile\.type = TILE_FOLDER;\s*tile\.view_id = 0;\s*tile\.title = hometiles_title::normalize\(server\.arg\("title"\)\.c_str\(\)\)\.c_str\(\);\s*tile\.icon_name = server\.arg\("icon_name"\);\s*tile\.span_w = 1;\s*tile\.span_h = 1;\s*tile\.layout_hidden = true;/);
assert.match(save, /apply_navigate_fields_from_request\(server, tile, TileConfig::rootFolderId\(\), tileConfig, error\)/);
assert.match(save, /new_folder = getNavigateTargetId\(tile\);\s*if \(!tileConfig\.saveFolderGrid\(TileConfig::rootFolderId\(\), \*home\)\) \{\s*tileConfig\.deleteFolder\(new_folder\);/);
assert.doesNotMatch(save, /folder_prefix/, 'Named by the save path, no second write');
assert.match(save, /wanted\[new_slot\] = new_folder;/);
assert.ok(save.indexOf('wanted[new_slot] = new_folder;') < save.indexOf('std::vector<Spot> spots;'), 'Made before the spots');
assert.match(save, /if \(new_folder\) \{\s*json \+= ",\\"folder\\":";[\s\S]*?",\\"index\\":";[\s\S]*?appendJsonEscaped\(json, home->tiles\[new_index\]\.title\);/);
// A new folder is named like its tab and gets the folder icon; an existing
// one keeps what it has; the panel never names it "Ordner" in every
// language.
const navigate = readRepoFile('src/types/navigate/web_handler.cpp').replace(/\r\n/g, '\n');
assert.match(navigate, /target_id = new_id;[\s\S]*?if \(!tile\.title\.length\(\)\) \{\s*tile\.title = hometiles_title::normalize\(TileConfig::unnamedFolderName\(target_id\)\.c_str\(\)\)\.c_str\(\);\s*\}\s*if \(!tile\.icon_name\.length\(\)\) tile\.icon_name = "folder";\s*\}\s*tileConfig\.updateFolder\(target_id, tile\.title, tile\.icon_name\);/);
const config = readRepoFile('src/tiles/config/tile_config.cpp').replace(/\r\n/g, '\n');
assert.doesNotMatch(config, /safe_name = "Ordner"/);
assert.match(config, /String TileConfig::unnamedFolderName\(uint16_t folder_id\) \{\s*return String\(i18n::strings\(configManager\.getConfig\(\)\.language\)\.folder_prefix\) \+ String\(folder_id\);/);
assert.match(readRepoFile('tools/icons/core-icons.txt'), /^folder\r?$/m, 'The folder icon is built in');
// The tile save answers with the saved title and icon.
const saveTiles = tiles.slice(tiles.indexOf('void WebAdminServer::handleSaveTiles() {'), tiles.indexOf('void WebAdminServer::handleReorderTiles() {'));
assert.match(saveTiles, /response \+= ",\\"title\\":\\"";\s*appendJsonEscaped\(response, tile\.title\);\s*response \+= "\\",\\"icon_name\\":\\"";\s*appendJsonEscaped\(response, tile\.icon_name\);/);
// A page's hidden tile gets no place in the active bar layout; the classic
// one gives a new tile the first free place, else its storage.
assert.ok(config.includes('if (!tile.layout_hidden && tile.type != TILE_BACK) {\n      tile_layouts::set(active, folder_id, tile.view_id,'));
assert.match(config, /const bool free = own \|\| findFreeClassicPlace\(occupied, tile, place\);\s*tile_layouts::set_classic_hidden\(folder_id, tile\.view_id, !free\);/);
// A deleted folder: the panel says so (the Web Admin reloads without its
// tab) and shows it at once, by the tile's Delete and by "Delete folder":
// Home when it showed the folder or one inside it, a page's dot gone.
assert.match(saveTiles, /deleted_view = deletedFolderView\(target_id\);\s*if \(!tileConfig\.deleteFolder\(target_id\)\)/);
assert.match(saveTiles, /if \(!shown_before_save && !deleted_view\.shown_inside && tileConfig\.getActiveFolderId\(\) == folder_id &&/);
assert.match(saveTiles, /if \(deleting_folder\) showFolderDeleted\(deleted_view\);/);
assert.match(saveTiles, /if \(deleting_folder && getNavigateTargetId\(previous_tile\)\) \{\s*response \+= ",\\"folder_deleted\\":";/);
const deleteFolder = tiles.slice(tiles.indexOf('void WebAdminServer::handleDeleteFolder() {'));
assert.ok(deleteFolder.indexOf('deletedFolderView(folder_id)') < deleteFolder.indexOf('tileConfig.deleteFolder(folder_id)'), 'Seen before it goes');
assert.match(deleteFolder, /if \(!view\.shown_inside && tileConfig\.getActiveFolderId\(\) == parent_id &&\s*!\(!powerManager\.isInSleep\(\) && tiles_show_active_layout_now\(\)\)\) \{\s*tiles_request_reload_if_loaded\(GridType::TAB0\);\s*\}\s*showFolderDeleted\(view\);/);
const helper = tiles.slice(tiles.indexOf('static DeletedFolderView deletedFolderView('), tiles.indexOf('static void showFolderDeleted('));
assert.match(helper, /view\.page = tile_layouts::page_number\(folder_id\) > 0;/);
assert.match(helper, /if \(shown == folder_id\) \{\s*view\.shown_inside = true;/);
assert.match(tiles, /static void showFolderDeleted\(const DeletedFolderView& view\) \{\s*if \(view\.page\) tiles_rebuild_heads\(!powerManager\.isInSleep\(\)\);\s*if \(view\.shown_inside\) tiles_switch_to_folder\(TileConfig::rootFolderId\(\)\);/);
// The Web Admin goes by that word, not by the edited (already empty) type.
const autosave = readRepoFile('src/web/admin/tiles/autosave.js').replace(/\r\n/g, '\n');
assert.doesNotMatch(autosave, /previousType === 4 && typeValue === '0'/);
assert.match(autosave, /const deletedFolder = Number\(data\.folder_deleted\) \|\| 0;\s*if \(deletedFolder > 0\) \{\s*forgetFolderTabSessionFragment\(deletedFolder\);/);
console.log('Page field add: picks stay (tile, empty cell, page field), + makes a page fast, new folders are named, deleting frees the field');
