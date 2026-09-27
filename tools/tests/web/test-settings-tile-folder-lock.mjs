// A parked Settings tile must not inherit the previous folder's type lock.
// Exercise selection, draft restoration and the real preview with delivered JS.
import {readAdminDeliverySource, readRepoFile, inlineScriptSafe} from '../../lib/admin-source.mjs';
import {runDomHarness} from '../../lib/headless-dom.mjs';

const html = `<!doctype html><html lang="en"><head>
<style>${readRepoFile('src/web/assets/admin.css')}</style></head><body>
<div id="tab-tiles-folder0" class="tile-tab tab-content active"><div class="tile-grid">
  <div class="tile" id="folder0-tile-0" data-type="4"></div>
  <div class="tile" id="folder0-tile-1" data-type="7"></div>
  <div class="tile" id="folder0-tile-2" data-type="4"></div>
</div></div>
<div id="settingsHiddenTile" class="tile settings-hidden-tile" data-hidden="1"
     data-title="Settings" data-icon="cog" data-col="1" data-row="0"
     data-span-w="1" data-span-h="1"></div>
<div id="folder0Settings" class="tile-settings"><div class="tile-specific-settings">
  <div class="tile-settings-head">
    <select id="folder0_tile_type"><option value="0">Empty</option>
      <option value="4">Folder</option><option value="5">Switch</option></select>
    <p class="hint hidden" id="folder0_tile_type_hint">Folder type locked</p>
  </div>
  <div class="tile-settings-body">
    <label>Title</label><textarea id="folder0_tile_title"></textarea>
    <input id="folder0_tile_icon"><input type="color" id="folder0_tile_color">
    ${['col', 'row', 'span_w', 'span_h'].map(name =>
      `<input id="folder0_tile_${name}" type="number" value="1">`).join('')}
    <div class="type-fields folder-pin-fields is-hidden" id="folder0_navigate_fields">
      <input type="checkbox" id="folder0_folder_pin_enabled">
      <label class="folder-pin-label is-hidden">PIN</label>
      <div class="folder-pin-control"><input id="folder0_folder_pin" type="password"></div>
      <span id="folder0_folder_pin_status"></span>
    </div>
    <div class="type-fields settings-access-tile-fields" id="folder0_settings_access_fields">
      <input type="checkbox" id="folder0_settings_tile_hidden" checked>
    </div>
  </div>
</div></div><pre id="result"></pre><script>
// Suppress unrelated startup timers and replace only the network boundary.
const nativeListen = document.addEventListener.bind(document);
document.addEventListener = (name, ...args) => {
  if (name !== 'DOMContentLoaded') nativeListen(name, ...args);
};
const APP_I18N = {}, CLIMATE_I18N = {}, BINARY_SENSOR_I18N = {};
const GRID_COLS = 7, GRID_ROWS = 5, TILES_PER_GRID = 35, ADMIN_WEB_SESSION_TOKEN = 'test';
const TILE_TYPE_REGISTRY = {
  0: {label: 'Empty', css: 'empty'},
  4: {label: 'Folder', css: 'navigate', fields: 'navigate', preview: 'none',
      load: 'loadNavigateFields', reset: 'resetNavigateFields'},
  5: {label: 'Switch', css: 'switch'},
  7: {label: 'Settings', css: 'navigate', fields: 'settings_access', preview: 'none',
      locked: true, reset: 'resetNavigateFields'}
};
const TILE_TABS = [], TAB_BY_FOLDER = {}, FOLDER_BY_TAB = {};
const SCREENSAVER_FOLDER_ID = 65535, SCREENSAVER_TILE_DEFAULT_OPACITY = 0;
const SCREENSAVER_TILE_DEFAULT_COLOR = '#000000';
const MEDIA_TILE_TYPE = 15, MEDIA_TILE_MIN_SPAN = 2, MEDIA_TILE_MAX_SPAN = 3;
window.fetch = async url => {
  if (url === '/api/entity-options') return {ok: true, json: async () => ({})};
  throw Error('Unexpected network request: ' + url);
};
${inlineScriptSafe(readAdminDeliverySource())}
try {
  const $ = id => document.getElementById('folder0_' + id);
  const check = (value, message) => { if (!value) throw Error(message); };
  const visible = element => element.getClientRects().length > 0;
  folderByTab.folder0 = 0;
  tileDataLoadedTabs.add('folder0');
  tilesData.folder0 = [
    {type: 4, title: 'Full folder', navigate_target: 1, folder_empty: false,
     folder_pin_enabled: true, folder_pin: '1234', col: 0, row: 0, span_w: 1, span_h: 1},
    {type: 7, title: 'Settings', icon_name: 'cog', col: 1, row: 0, span_w: 1, span_h: 1},
    {type: 4, title: 'Empty folder', navigate_target: 2, folder_empty: true,
     col: 2, row: 0, span_w: 1, span_h: 1}
  ];
  const checkSettings = title => {
    check(!visible($('tile_type_hint')), 'Settings must not show the folder type-lock hint');
    check($('tile_type').value === '7' && $('tile_type').disabled,
      'Settings keeps its fixed type');
    check($('tile_type').selectedOptions[0]?.textContent === 'Settings',
      'The type field must show Settings, not an empty selection');
    check(!visible($('navigate_fields')) && !visible($('folder_pin')),
      'Settings must not expose an empty folder PIN field');
    check(visible($('settings_access_fields')), 'Settings access controls remain visible');
    check($('tile_title').value === title, 'Settings restores its own title');
  };
  const selectFullFolder = () => {
    selectTile(0, 'folder0');
    check(visible($('tile_type_hint')), 'A full folder still shows its type-lock hint');
    check($('tile_type').querySelector('option[value="5"]').disabled,
      'A full folder cannot change to Switch');
    check(!$('tile_type').querySelector('option[value="0"]').disabled,
      'A full folder can still be emptied');
    check(visible($('folder_pin')), 'The folder PIN field is visible when enabled');
  };
  selectTile(1, 'folder0');
  checkSettings('Settings');
  selectFullFolder();
  selectTile(1, 'folder0');
  checkSettings('Settings');
  // Both paths share the panel with a previously selected full folder.
  for (const dirty of [false, true]) {
    drafts.folder0 = dirty ? {'-2': {_dirty: true, type: '7', title: 'My settings',
      icon: 'cog', col: '2', row: '1', span_w: '1', span_h: '1'}} : {};
    selectFullFolder();
    selectHiddenSettingsTile();
    checkSettings(dirty ? 'My settings' : 'Settings');
    selectTile(2, 'folder0');
    check(!visible($('tile_type_hint')), 'An empty folder has no lock hint');
    check(!$('tile_type').disabled && !$('tile_type').querySelector('option[value="5"]').disabled,
      'An empty folder can change type after leaving Settings');
  }
  document.body.dataset.result = 'pass';
} catch (error) {
  document.body.dataset.result = 'fail';
  document.getElementById('result').textContent = error.stack;
}
</script></body></html>`;

try {
  runDomHarness({label: 'Settings tile folder lock', html,
    tmpPrefix: 'hometiles-settings-folder-lock-'});
} catch (error) {
  throw Error(error.message.match(/<pre id="result">([\s\S]*?)<\/pre>/)?.[1] || error.message.slice(0, 400));
}
