// Settings tile out of the Home parking slot and back, with the delivered
// Web Admin code and a simulated device (user 2026-10-02): the move and the
// teal selection show at once, the slot keeps its drop target and hint after
// a round trip, and a second move during a slow save is not lost.
import {readAdminDeliverySource, readRepoFile, inlineScriptSafe} from '../../lib/admin-source.mjs';
import {runDomHarness} from '../../lib/headless-dom.mjs';

const TILES = 35;
const html = `<!doctype html><html><head><style>
${readRepoFile('src/web/assets/admin.css')}
:root { --grid-cols:7; --grid-rows:5; --preview-cell-w:100px; --preview-cell-h:100px; --preview-gap:10px; --preview-pad:0px; }
</style></head><body>
<div id="tab-tiles-folder0" class="tile-tab tab-content active"><div class="tile-grid">
${Array.from({length: TILES}, (_, i) => `<div class="tile empty" id="folder0-tile-${i}" data-index="${i}" data-type="0" draggable="true"></div>`).join('')}
</div>
<div class="settings-hidden-parking"><div id="settingsHiddenSlot" class="tile-grid settings-hidden-slot"><div id="settingsHiddenTile"
 class="tile settings-hidden-tile empty" draggable="false" data-hidden="0" data-title="Settings" data-icon="cog"
 data-bg-color="0" data-col="0" data-row="0" data-span-w="1" data-span-h="1"><i class="mdi mdi-tray-arrow-down tile-icon"></i></div></div>
<div class="settings-parking-texts"><div id="settingsHiddenHint" class="settings-hidden-hint">Drop Settings here</div></div></div>
</div>
<div id="folder0Settings" class="tile-settings"><div class="tile-specific-settings">
<select id="folder0_tile_type"><option value="0">Empty</option><option value="4">Folder</option><option value="7">Settings</option></select>
<textarea id="folder0_tile_title"></textarea><input id="folder0_tile_icon"><input type="color" id="folder0_tile_color">
${['col','row','span_w','span_h'].map(name => `<input id="folder0_tile_${name}" type="number" value="1">`).join('')}
<div class="type-fields" id="folder0_settings_access_fields">
<input id="folder0_settings_pin_enabled" type="checkbox" data-pin-configured="0">
<input id="folder0_settings_tile_hidden" type="checkbox">
<input id="folder0_settings_swipe_enabled" type="checkbox">
<select id="folder0_settings_reveal_edge"><option value="0">Left</option></select>
</div></div></div><pre id="result">running</pre><script>
const nativeListen = document.addEventListener.bind(document);
document.addEventListener = (name, ...args) => { if (name !== 'DOMContentLoaded') nativeListen(name, ...args); };
const APP_I18N = {}, CLIMATE_I18N = {}, BINARY_SENSOR_I18N = {};
const GRID_COLS = 7, GRID_ROWS = 5, TILES_PER_GRID = ${TILES}, ADMIN_WEB_SESSION_TOKEN = 'test';
const TILE_TYPE_REGISTRY = {
  4: {label:'Folder',css:'navigate',fields:'navigate',preview:'none',load:'loadNavigateFields',reset:'resetNavigateFields'},
  7: {label:'Settings',css:'navigate',fields:'settings_access',preview:'none',locked:true,reset:'resetNavigateFields'}
};
const TILE_TABS = [], TAB_BY_FOLDER = {}, FOLDER_BY_TAB = {}, SCREENSAVER_FOLDER_ID = 65535;
const SCREENSAVER_TILE_DEFAULT_OPACITY = 0, SCREENSAVER_TILE_DEFAULT_COLOR = '#000000';
const MEDIA_TILE_TYPE = 15, MEDIA_TILE_MIN_SPAN = 2, MEDIA_TILE_MAX_SPAN = 3;
// The simulated device: its stored Home grid, answering after a delay like a
// panel that writes flash.
const empty = () => ({type:0});
const device = Array.from({length: ${TILES}}, empty);
device[0] = {type:4,title:'Folder',navigate_target:1,col:0,row:0,span_w:1,span_h:1};
device[1] = {type:7,title:'Settings',icon_name:'cog',col:2,row:0,span_w:1,span_h:1};
let saveDelay = 30;
const log = [];
const wait = ms => new Promise(resolve => setTimeout(resolve, ms));
window.fetch = async (url, options) => {
  if (options?.method === 'POST') {
    const body = Object.fromEntries(new URLSearchParams(options.body));
    log.push(url + ' ' + JSON.stringify(body));
    await wait(saveDelay);
    if (url === '/mqtt' && body.settings_access_present) {
      const at = device.findIndex(tile => tile.type === 7);
      if (body.settings_tile_hidden === '1') { if (at >= 0) device[at] = empty(); }
      else if (at < 0) {
        const free = device.findIndex(tile => !tile.type);
        device[free] = {type:7,title:'Settings',icon_name:'cog',col:Number(body.settings_tile_target_col),
          row:Number(body.settings_tile_target_row),span_w:1,span_h:1};
      }
      return {ok:true, json:async()=>({ok:true,reload:false,settings_pin:''})};
    }
    return {ok:true, json:async()=>({ok:true,success:true})};
  }
  if (String(url).startsWith('/api/tiles?folder=0')) {
    await wait(10);
    return {ok:true, json:async()=>JSON.parse(JSON.stringify(device))};
  }
  if (url === '/api/entity-options') return {ok:true, json:async()=>({})};
  return {ok:true, json:async()=>({})};
};
${inlineScriptSafe(readAdminDeliverySource())}
(async () => { try {
  const check = (value, message) => { if (!value) throw Error(message + '\\n' + log.join('\\n')); };
  const slot = document.getElementById('settingsHiddenSlot');
  const parked = document.getElementById('settingsHiddenTile');
  const hint = document.getElementById('settingsHiddenHint');
  const grid = getTileGrid('folder0');
  const cell = el => el.getBoundingClientRect();
  const at = (el, dx = 20, dy = 20) => ({clientX: cell(el).left + dx, clientY: cell(el).top + dy});
  const settingsElement = () => [...grid.children].find(el => el.dataset.type === '7');
  const placeholder = () => grid.querySelector('.tile-drop-placeholder.show');
  const activeIds = () => [...document.querySelectorAll('.tile.active')].filter(el => el !== dragPreview).map(el => el.id).join(',');
  folderByTab.folder0 = 0;
  tileDataLoadedTabs.add('folder0');
  tilesData.folder0 = JSON.parse(JSON.stringify(device));
  tilesData.folder0.forEach((tile, i) => renderTileFromData('folder0', i, tile, {}));
  layoutTiles('folder0', tilesData.folder0);
  enableTileDrag('folder0');
  enableSettingsHiddenSlot();
  selectTile(1, 'folder0');

  const park = async () => {
    const tile = settingsElement();
    check(tile, 'Settings is in the grid before parking');
    const transfer = new DataTransfer();
    tile.dispatchEvent(new DragEvent('dragstart', {bubbles:true,cancelable:true,dataTransfer:transfer,...at(tile)}));
    grid.dispatchEvent(new DragEvent('dragover', {bubbles:true,cancelable:true,dataTransfer:transfer,...at(tile)}));
    check(placeholder(), 'Over the grid the teal placeholder shows in the grid');
    slot.dispatchEvent(new DragEvent('dragover', {bubbles:true,cancelable:true,dataTransfer:transfer,...at(slot)}));
    check(slot.classList.contains('drop-target'), 'The parking slot shows its drop target');
    check(!placeholder(), 'Over the slot the grid placeholder goes, like moving between grid cells');
    check(getComputedStyle(parked).borderTopStyle === 'dashed' &&
          getComputedStyle(parked).borderTopColor === 'rgb(38, 166, 154)', 'The slot target is the teal placeholder');
    slot.dispatchEvent(new DragEvent('drop', {bubbles:true,cancelable:true,dataTransfer:transfer,...at(slot)}));
    tile.dispatchEvent(new DragEvent('dragend', {bubbles:true,dataTransfer:transfer}));
  };
  // The pointer that puts the tile, taken 20 px into it, on a grid cell.
  const cellPoint = (col, row) => {
    const m = getTileGridMetrics('folder0');
    return {clientX: m.rect.left + m.padLeft + col * (m.cellW + m.gapX) + 20,
            clientY: m.rect.top + m.padTop + row * (m.cellH + m.gapY) + 20};
  };
  const unpark = async (col, row) => {
    check(parked.dataset.hidden === '1', 'Settings is parked before restoring');
    const transfer = new DataTransfer();
    parked.dispatchEvent(new DragEvent('dragstart', {bubbles:true,cancelable:true,dataTransfer:transfer,...at(parked)}));
    grid.dispatchEvent(new DragEvent('dragover', {bubbles:true,cancelable:true,dataTransfer:transfer,...cellPoint(col, row)}));
    grid.dispatchEvent(new DragEvent('drop', {bubbles:true,cancelable:true,dataTransfer:transfer,...cellPoint(col, row)}));
    parked.dispatchEvent(new DragEvent('dragend', {bubbles:true,dataTransfer:transfer}));
  };
  const settle = () => wait(400);

  // Park: the slot shows the tile and the teal selection at once.
  await park();
  check(parked.dataset.hidden === '1' && !settingsElement(), 'Parking shows at once');
  check(activeIds() === 'settingsHiddenTile', 'The selection moves to the slot at once: ' + activeIds());
  await settle();
  check(parked.dataset.hidden === '1' && hint.classList.contains('is-hidden'), 'Parked after the save');

  // Taking the parked tile works like taking a grid tile: it is selected (the
  // drag image carries the teal selection), the slot it left looks empty, and
  // the teal placeholder follows the pointer between the slot and the grid.
  // Dropped back on the slot it stays parked without a save.
  {
    selectTile(0, 'folder0');
    const posts = log.length;
    const transfer = new DataTransfer();
    parked.dispatchEvent(new DragEvent('dragstart', {bubbles:true,cancelable:true,dataTransfer:transfer,...at(parked)}));
    check(activeIds() === 'settingsHiddenTile' && dragPreview?.classList.contains('active'),
          'Taking the parked tile selects it: ' + activeIds());
    check(slot.classList.contains('lifting') && getComputedStyle(parked.querySelector('.tile-icon')).visibility === 'hidden',
          'The slot looks empty while its tile is dragged');
    slot.dispatchEvent(new DragEvent('dragover', {bubbles:true,cancelable:true,dataTransfer:transfer,...at(parked)}));
    check(slot.classList.contains('drop-target') && getComputedStyle(parked).opacity === '1' &&
          getComputedStyle(parked).borderTopColor === 'rgb(38, 166, 154)', 'Over the slot the teal placeholder shows there');
    slot.dispatchEvent(new DragEvent('dragleave', {bubbles:true,relatedTarget:grid,dataTransfer:transfer}));
    grid.dispatchEvent(new DragEvent('dragover', {bubbles:true,cancelable:true,dataTransfer:transfer,...cellPoint(4, 2)}));
    check(placeholder() && !slot.classList.contains('drop-target'), 'Over the grid the placeholder moves into the grid');
    slot.dispatchEvent(new DragEvent('dragover', {bubbles:true,cancelable:true,dataTransfer:transfer,...at(parked)}));
    check(!placeholder() && slot.classList.contains('drop-target'), 'Back over the slot the grid placeholder goes');
    slot.dispatchEvent(new DragEvent('drop', {bubbles:true,cancelable:true,dataTransfer:transfer,...at(parked)}));
    parked.dispatchEvent(new DragEvent('dragend', {bubbles:true,dataTransfer:transfer}));
    await settle();
    check(parked.dataset.hidden === '1' && !slot.classList.contains('lifting') && log.length === posts,
          'Dropped back on the slot it stays parked without a save');
  }

  // Restore to an empty cell: grid tile and selection at once, the slot empty
  // with its hint.
  await unpark(4, 2);
  check(settingsElement() && parked.dataset.hidden === '0', 'Restoring shows at once');
  check(activeIds() === settingsElement().id, 'The selection moves to the grid at once: ' + activeIds());
  check(!hint.classList.contains('is-hidden'), 'The empty slot shows its hint at once');
  await settle();
  check(settingsElement() && parked.dataset.hidden === '0' && !hint.classList.contains('is-hidden'), 'Restored after the save');

  // And back into the slot: drop target, hint and move as the first time.
  await park();
  check(parked.dataset.hidden === '1', 'Parking again shows at once');
  await settle();
  check(parked.dataset.hidden === '1' && device.every(tile => tile.type !== 7), 'Parked again on the device');

  // A slow device: restoring and parking again before the first save ends.
  saveDelay = 300;
  await unpark(4, 2);
  await wait(50);
  await park();
  check(parked.dataset.hidden === '1', 'The second move shows although the first save still runs');
  await wait(1200);
  check(parked.dataset.hidden === '1' && device.every(tile => tile.type !== 7),
        'The device ends parked like the preview');
  document.body.dataset.result = 'pass';
} catch (error) {
  document.body.dataset.result = 'fail'; document.getElementById('result').textContent = error.stack;
}})();
</script></body></html>`;

try {
  runDomHarness({label: 'Settings parking roundtrip', html, tmpPrefix: 'hometiles-settings-parking-',
    extraArgs: ['--virtual-time-budget=10000']});
} catch (error) {
  throw Error(error.message.match(/<pre id="result">([\s\S]*?)<\/pre>/)?.[1] || error.message.slice(0, 600));
}
console.log('Settings parking: moves, selection and slot hint stay right over round trips');
