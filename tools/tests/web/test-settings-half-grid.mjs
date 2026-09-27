// Settings uses the Folder geometry, including 1x0.5, through the delivered
// selection, preview, drag, resize, autosave and hidden-tile request paths.
import {readAdminDeliverySource, readRepoFile, inlineScriptSafe} from '../../lib/admin-source.mjs';
import {runDomHarness} from '../../lib/headless-dom.mjs';

const html = `<!doctype html><html><head><style>
${readRepoFile('src/web/assets/admin.css')}
:root { --grid-cols:7; --grid-rows:5; --preview-cell-w:100px; --preview-cell-h:100px; --preview-gap:10px; --preview-pad:0px; }
</style></head><body>
<div id="tab-tiles-folder0" class="tile-tab tab-content active"><div class="tile-grid">
<div class="tile navigate" id="folder0-tile-0" data-index="0" data-type="7" draggable="true"></div>
<div class="tile navigate" id="folder0-tile-1" data-index="1" data-type="4" draggable="true"></div>
</div></div>
<div id="settingsHiddenSlot"><div id="settingsHiddenTile" class="tile" data-hidden="1"
 data-title="Settings" data-icon="cog" data-col="0.5" data-row="0.5"
 data-span-w="1.5" data-span-h="0.5" draggable="true"></div></div>
<div id="folder0Settings" class="tile-settings"><div class="tile-specific-settings">
<select id="folder0_tile_type"><option value="4">Folder</option></select>
<textarea id="folder0_tile_title"></textarea><input id="folder0_tile_icon"><input type="color" id="folder0_tile_color">
${['col','row','span_w','span_h'].map(name => `<input id="folder0_tile_${name}" type="number" value="1">`).join('')}
<div class="type-fields" id="folder0_settings_access_fields">
<input id="folder0_settings_pin_enabled" type="checkbox">
<input id="folder0_settings_tile_hidden" type="checkbox">
<input id="folder0_settings_swipe_enabled" type="checkbox">
<select id="folder0_settings_reveal_edge"><option value="0">Left</option></select>
</div></div></div><pre id="result"></pre><script>
const nativeListen = document.addEventListener.bind(document);
document.addEventListener = (name, ...args) => { if (name !== 'DOMContentLoaded') nativeListen(name, ...args); };
const APP_I18N = {}, CLIMATE_I18N = {}, BINARY_SENSOR_I18N = {};
const GRID_COLS = 7, GRID_ROWS = 5, TILES_PER_GRID = 35, ADMIN_WEB_SESSION_TOKEN = 'test';
const TILE_TYPE_REGISTRY = {
  4: {label:'Folder',css:'navigate',fields:'navigate',preview:'none',load:'loadNavigateFields',reset:'resetNavigateFields'},
  7: {label:'Settings',css:'navigate',fields:'settings_access',preview:'none',locked:true,reset:'resetNavigateFields'}
};
const TILE_TABS = [], TAB_BY_FOLDER = {}, FOLDER_BY_TAB = {}, SCREENSAVER_FOLDER_ID = 65535;
const SCREENSAVER_TILE_DEFAULT_OPACITY = 0, SCREENSAVER_TILE_DEFAULT_COLOR = '#000000';
const MEDIA_TILE_TYPE = 15, MEDIA_TILE_MIN_SPAN = 2, MEDIA_TILE_MAX_SPAN = 3;
const posts = [];
window.fetch = async (url, options) => {
  if (options?.method === 'POST') {
    posts.push({url, ...Object.fromEntries(new URLSearchParams(options.body))});
    return {ok:true, json:async()=>({ok:true,success:true})};
  }
  if (url === '/api/entity-options') return {ok:true, json:async()=>({})};
  throw Error('Unexpected request: ' + url);
};
${inlineScriptSafe(readAdminDeliverySource())}
(async () => { try {
  const $ = name => document.getElementById('folder0_' + name);
  const check = (value, message) => { if (!value) throw Error(message); };
  folderByTab.folder0 = 0;
  tileDataLoadedTabs.add('folder0');
  tilesData.folder0 = [
    {type:7,title:'Settings',icon_name:'cog',col:0.5,row:0.5,span_w:1.5,span_h:0.5},
    {type:4,title:'Folder',navigate_target:1,col:4,row:0,span_w:1,span_h:1}
  ];
  layoutTiles('folder0', tilesData.folder0);
  selectTile(0, 'folder0');
  check($('tile_col').value === '1.5' && $('tile_row').value === '1.5', 'Selection preserves half-cell positions');
  check($('tile_span_w').value === '1.5' && $('tile_span_h').value === '0.5', 'Selection preserves half-cell size');
  for (const name of ['col','row','span_w','span_h']) check($('tile_' + name).step === '0.5', 'Half-step editor: ' + name);
  const card = document.getElementById('folder0-tile-0');
  check(card.classList.contains('compact-title-only'), 'Half-height Settings uses the Folder compact preview');
  check(card.querySelectorAll('.tile-resize-handle').length === 3, 'Settings has resize handles');

  // The real resize candidate and pointer lifecycle must reach 1x0.5 and grow.
  const metrics = getTileGridMetrics('folder0');
  const pointer = (col, row) => ({clientX:metrics.rect.left + metrics.padLeft + col * (metrics.cellW + metrics.gapX),
    clientY:metrics.rect.top + metrics.padTop + row * (metrics.cellH + metrics.gapY)});
  const resize = (col, row) => {
    beginTileResize('folder0', card, 'se', {preventDefault(){}, stopPropagation(){}});
    handleTileResizeMove({preventDefault(){}, ...pointer(col,row)});
    handleTileResizeEnd();
  };
  resize(1, 0.5);
  check($('tile_span_w').value === '1' && $('tile_span_h').value === '0.5', 'Settings shrinks to 1x0.5');
  resize(2, 1.5);
  check($('tile_span_w').value === '2' && $('tile_span_h').value === '1.5', 'Settings grows in half steps');
  await new Promise(resolve => setTimeout(resolve,350));
  const resized = posts.findLast(post => post.url === '/api/tiles');
  check(resized?.span_w === '2' && resized?.span_h === '1.5', 'Autosave includes resized Settings geometry');
  renderTileFromData('folder0', 0, tilesData.folder0[0], {});
  check(card.dataset.spanH === '1.5', 'Cached preview preserves the new height');

  enableTileDrag('folder0');
  const transfer = new DataTransfer();
  card.dispatchEvent(new DragEvent('dragstart',{bubbles:true,cancelable:true,dataTransfer:transfer,...pointer(0.6,0.6)}));
  const grid = getTileGrid('folder0');
  for (const name of ['dragover','drop']) grid.dispatchEvent(new DragEvent(name,
    {bubbles:true,cancelable:true,dataTransfer:transfer,...pointer(2.6,2.1)}));
  card.dispatchEvent(new DragEvent('dragend',{dataTransfer:transfer}));
  await Promise.resolve();
  const moved = posts.findLast(post => post.url === '/api/tiles/reorder');
  check(moved?.target_col === '2.5' && moved?.target_row === '2', 'Drag sends half-cell target coordinates');
  check(tilesData.folder0[0].col === 2.5 && tilesData.folder0[0].row === 2, 'Local drag result matches the saved target');

  // Parked Settings retains fractions in drafts and access-save payloads.
  $('settings_tile_hidden').checked = true;
  selectHiddenSettingsTile();
  check($('tile_span_w').value === '1.5' && $('tile_span_h').value === '0.5', 'Parked Settings retains its size');
  const saved = await saveSettingsAccess(null, {col:2.5,row:2.5},
    normalizeHiddenSettingsSnapshot(), readSettingsAccessState(), false);
  check(saved, 'Parked Settings save succeeds');
  const parked = posts.at(-1);
  check(parked.settings_tile_col === '0.5' && parked.settings_tile_row === '0.5', 'Parked snapshot keeps fractional position');
  check(parked.settings_tile_span_w === '1.5' && parked.settings_tile_span_h === '0.5', 'Parked snapshot keeps fractional size');
  check(parked.settings_tile_target_col === '2.5' && parked.settings_tile_target_row === '2.5', 'Restore target keeps fractional position');
  enableSettingsHiddenSlot();
  const hidden = document.getElementById('settingsHiddenTile');
  hidden.dispatchEvent(new DragEvent('dragstart',{bubbles:true,cancelable:true,dataTransfer:new DataTransfer()}));
  check(dragSource.layout.span_w === 1.5 && dragSource.layout.span_h === 0.5, 'Parked drag preserves its size');
  hidden.dispatchEvent(new DragEvent('dragend'));
  document.body.dataset.result = 'pass';
} catch (error) {
  document.body.dataset.result = 'fail'; document.getElementById('result').textContent = error.stack;
}})();
</script></body></html>`;
try {
  runDomHarness({label:'Settings half-grid',html,tmpPrefix:'hometiles-settings-half-grid-',
    extraArgs:['--virtual-time-budget=2000']});
} catch (error) {
  throw Error(error.message.match(/<pre id="result">([\s\S]*?)<\/pre>/)?.[1] || error.message.slice(0,400));
}
