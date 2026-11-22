// The Web Admin's Camera tile (mockup build/design-mockups/webcam-radar/radar-tall.html):
// one source, under it rows that fold out in their own frame, one open at a time;
// adding only adds a row; removing only in the details; the shown entry fills the
// tile's own fields, so previews, drafts and saving work as for every tile. Runs the
// whole delivered editor in a browser against the real CSS.
import assert from 'node:assert/strict';
import {extractDeliveredFunction, inlineScriptSafe, readRepoFile} from '../../lib/admin-source.mjs';
import {runDomHarness} from '../../lib/headless-dom.mjs';

const admin = readRepoFile('src/types/camera/admin.js');
const html = readRepoFile('src/types/camera/web_html.cpp');
const scripts = readRepoFile('src/types/camera/web_scripts.cpp');

// The fields of the panel: Quelle, the list, then what applies to the whole tile.
const source = html.indexOf('"camera_source", tr.media_source');
const list = html.indexOf('_camera_list');
const display = html.indexOf('"camera_display", tr.camera_show');
const shows = html.indexOf('tr.camera_tile_shows');
const tap = html.indexOf('"camera_tap", tr.camera_tap');
assert.ok(source > 0 && source < list && list < display && display < shows && shows < tap, 'The mockup\'s order');
assert.doesNotMatch(html, /appendEntityPickerField/, 'The entity pickers belong to the entries');
// Like every tile, only with more entities: source and entries where a tile's entity
// stands, the options under Advanced.
assert.ok(html.indexOf('data-tile-place=\\"head\\"') > 0 && html.indexOf('data-tile-place=\\"head\\"') < source, 'In the head');
assert.doesNotMatch(html, /camera_tile_group/, 'No heading of its own');
assert.match(scripts, /tr\.camera_site_blocked/, 'A site that refuses is named');
assert.match(admin, /state === 'blocked' \? 'siteBlocked' : 'noPictureFound'/);
// Every word through the central strings, in the type's own script block.
for (const key of ['camera_list_places', 'camera_add_entity', 'camera_add_place', 'camera_add_webcam', 'camera_own_coordinates',
                   'camera_no_picture_found', 'camera_latitude_range', 'camera_longitude_range', 'camera_tile_shows_last']) {
  assert.ok(scripts.includes(`tr.${key}`), key);
}
assert.doesNotMatch(admin, /Breitengrad|Regenradar|hinzuf/, 'No display text in the editor');

// The System page's emergency address, saved by itself.
const page = readRepoFile('src/web/server/render/web_admin_html.cpp');
assert.match(page, /id="radar_source"/);
assert.match(readRepoFile('src/web/server/web_admin.cpp'), /"\/api\/radar_source", HTTP_POST/);
assert.match(readRepoFile('src/web/server/web_admin.cpp'), /"\/api\/camera_sources", HTTP_GET/);

// The title and icon of the tile's head go away with this type.
assert.match(readRepoFile('src/web/admin/tiles/type-selection.js'), /_camera_fields'\)\);/);
const picker = readRepoFile('src/web/admin/tiles/entity-picker.js');
assert.match(picker, /tabs\.style\.display = open\.input\.dataset\.entityPicker === 'places' \? 'none' : '';/, 'No Recent or Favorites for places');
assert.match(picker, /open\.local = \[cameraOwnCoordinatesEntry\(\)\];/, '"Own coordinates" in the dropdown');
const iconPicker = readRepoFile('src/web/admin/tiles/tile-head.js');
assert.match(iconPicker, /entry: !!button\?\.dataset\.iconEntry/, 'An entry\'s icon offers Automatic and No icon');

const css = readRepoFile('src/web/assets/admin.css');
const harness = `<!doctype html><html><head><meta charset="utf-8"><style>${css.replaceAll('</', '<\\/')}</style></head><body data-result="running">
<div class="tile-settings"><div id="tSettings"><div class="tile-head-row"><textarea id="t_tile_title"></textarea><input id="t_tile_icon"></div>
<div class="tile-place-slot" id="t_tile_head_slot"></div>
<div class="tile-settings-body"><div class="tile-place-slot" id="t_tile_top_slot"></div>
<div class="tile-settings-group" id="t_tile_group_advanced">Advanced</div>
<div id="t_camera_fields" class="type-fields show">
<input type="hidden" id="t_camera_entity"><input type="hidden" id="t_camera_entries">
<div class="tile-place camera-tile-fields" data-tile-place="head">
<select id="t_camera_source"><option value="h">H</option><option value="r">R</option><option value="w">W</option></select>
<label id="t_camera_list_label"></label>
<div class="camera-entries-wrap" data-tab="t"><div class="camera-entries" id="t_camera_list"></div><button type="button" class="camera-entry-add" id="t_camera_add" data-camera-add></button></div>
</div>
<select id="t_camera_display"><option value="0">0</option><option value="1">1</option></select>
<div class="camera-shows-row hidden" id="t_camera_shows_row"><label>x</label><select id="t_camera_shows"></select></div>
<select id="t_camera_tap"><option value="0">0</option><option value="1">1</option></select>
<div id="t_camera_picture_fields"><select id="t_camera_fit"><option value="fill">f</option></select>
<select id="t_camera_head"><option value="auto">a</option></select>
<div id="t_camera_every_row"><input id="t_camera_every" value="10"></div>
<input id="t_camera_transparency" value="0"><output id="t_camera_transparency_value"></output></div>
<input type="hidden" id="t_tile_icon_color" value="#FFFFFF" data-unset="1">
</div></div></div></div>
<script>${inlineScriptSafe(`
const CAMERA_TEXT = Object.freeze({entity:'Picture or camera',listPlaces:'Places',listWebcams:'Webcams',place:'Place',address:'Address',
  addEntity:'Add picture',addPlace:'Add place',addWebcam:'Add webcam',newWebcam:'New webcam',placeHome:'Home',kindZone:'Zone',kindImage:'Picture',
  ownCoordinates:'Own coordinates',ownCoordinatesHint:'Enter',noCoordinates:'No coordinates yet',latitudeLabel:'Lat',longitudeLabel:'Lon',
  latitudeRange:'Latitude: from -90 to 90.',longitudeRange:'Longitude: from -180 to 180.',noPictureFound:'No picture',showsLast:'Last chosen',
  siteBlocked:'This site blocks',remove:'Remove',title:'Title',auto:'Auto',icon:'Icon'});
const entityRemoteInfo = new Map();
const sensorMetaCache = {names:{'camera.door':'Door'},icons:{}};
let previews = 0, drafts = 0, saves = 0;
function updateTilePreview() { previews++; }
function updateDraft() { drafts++; }
function scheduleAutoSave() { saves++; }
function escapeHtml(text) { const d = document.createElement('div'); d.textContent = String(text); return d.innerHTML.replaceAll('"', '&quot;'); }
function t(key) { return key; }
function normalizeMdiIconName(name) { return String(name || '').replace(/^mdi[:-]/, ''); }
function isExplicitlyDisabledValue(value) { return String(value) === 'none'; }
function titleFromEntity(entity) { return String(entity).split('.')[1] || ''; }
function normalizeTileTitle(title) { return String(title); }
function loadIconColorFields() {}
function saveIconColorFields() {}
function resetIconColorFields() {}
function setIconColorInput(tab, color) {
  const input = document.getElementById(tab + '_tile_icon_color');
  input.value = color || '#FFFFFF';
  input.dataset.unset = color ? '0' : '1';
}
function syncIconColorFields() {}
function syncSwitchChoices() {}
function setupEntityPickers() {}
function entityPickerEntryFor(input, value) {
  return {value, name: value === 'camera.door' ? 'Door' : value === 'zone.arbeit' ? 'Arbeit' : value.startsWith('geo:') ? 'Trento' : value,
    context: value === 'zone.arbeit' ? 'Home Assistant' : value.startsWith('geo:') ? 'Trentino, Italia' : ''};
}
${admin}
${extractDeliveredFunction('placeTileTypeParts')}
${extractDeliveredFunction('syncTileAdvancedCaption')}
function check(ok, what) { if (!ok) throw new Error(what); }
const $ = id => document.getElementById(id);
const click = selector => { const el = document.querySelector(selector); check(el, 'missing ' + selector); el.click(); };
const type = (id, value) => { const el = $(id); el.value = value; el.dispatchEvent(new Event('input', {bubbles: true})); };
const pick = value => { const el = $('t_camera_entry_value'); el.value = value; el.dispatchEvent(new Event('change', {bubbles: true})); };
const rows = () => [...document.querySelectorAll('.camera-entry')];
const saved = () => {
  const out = {};
  saveCameraFields('t', {append: (key, value) => { out[key] = value; }});
  return out;
};
try {
  // Source and entries in the head where a tile's entity stands, the options under Advanced.
  placeTileTypeParts('t');
  syncTileAdvancedCaption('t');
  check($('t_tile_head_slot').contains($('t_camera_source')) && $('t_tile_head_slot').contains($('t_camera_list')), 'in the head');
  check(!$('t_tile_top_slot').children.length, 'nothing else moved');
  check($('t_camera_fields').contains($('t_camera_display')) && $('t_camera_fields').contains($('t_camera_fit')), 'the options stay');
  check(!$('t_tile_group_advanced').classList.contains('hidden'), 'under Advanced');
  check($('t_tile_title').closest('.tile-head-row').classList.contains('hidden'), 'title and icon belong to the entries');
  // A tile from before the entries: its one entity becomes the first row.
  $('t_tile_title').value = 'Door';
  loadCameraFields('t', {sensor_entity: 'camera.door', sensor_display_mode: 1});
  check(rows().length === 1 && rows()[0].textContent.includes('Door'), 'an old tile keeps its entity');
  check(saved().camera_entries.startsWith('h\\tcamera.door'), 'and its record');
  check($('t_camera_shows_row').classList.contains('hidden'), 'one entry: nothing to choose');

  // Adding only adds a row, nothing opens by itself; an empty row is not saved.
  click('[data-camera-add]');
  check(rows().length === 2 && !document.querySelector('.camera-entry-body'), 'adding opens nothing');
  check(!saved().camera_entries.includes('\\n'), 'an empty row is not saved');
  check(document.querySelector('.camera-entry-remove') === null, 'no trash in a closed row');

  // A tap folds out in the frame, the next tap closes the other, the head stays.
  const head = rows()[0].querySelector('.camera-entry-head').outerHTML;
  click('[data-camera-toggle="0"]');
  check(rows()[0].querySelector('.camera-entry-body') && rows()[0].querySelector('.camera-entry-head').outerHTML === head, 'the row stays the same');
  click('[data-camera-toggle="1"]');
  check(!rows()[0].querySelector('.camera-entry-body') && rows()[1].querySelector('.camera-entry-body'), 'one open at a time');
  check(getComputedStyle(rows()[1]).borderTopWidth === getComputedStyle(rows()[0]).borderTopWidth, 'no other frame when open');

  // The picker's choice names the row and fills the record; a chosen entity resets title and icon.
  const saves0 = saves, drafts0 = drafts, previews0 = previews;
  pick('camera.garden');
  check(saves > saves0 && drafts > drafts0 && previews > previews0, 'every change: preview, draft and autosave');
  check(saved().camera_entries.split('\\n').length === 2, 'two entries');
  check(!$('t_camera_shows_row').classList.contains('hidden'), 'with two: the tile can choose');
  check([...$('t_camera_shows').options].map(o => o.value).join() === '0,1,last', 'all entries and last chosen');

  // Title: Auto until typed, Auto again brings the name back; the row follows at once.
  type('t_camera_entry_title', 'Garten');
  check(rows()[1].querySelector('.entity-picker-name').textContent === 'Garten', 'the row follows the title');
  check(!document.querySelector('[data-camera-auto]').classList.contains('on'), 'typing turns Auto off');
  click('[data-camera-auto]');
  check(document.querySelector('[data-camera-auto]').classList.contains('on') && $('t_camera_entry_title').value === 'camera.garden', 'Auto brings the name back');
  type('t_camera_entry_icon', 'cctv');
  check(rows()[1].querySelector('.entity-picker-icon i').className.includes('mdi-cctv'), 'the row follows the icon');

  // What the tile shows fills its own fields.
  $('t_camera_shows').value = '1';
  $('t_camera_shows').dispatchEvent(new Event('change', {bubbles: true}));
  cameraShowsChanged('t');
  check($('t_camera_entity').value === 'camera.garden' && $('t_tile_icon').value === 'cctv', 'the chosen entry fills the tile');
  $('t_camera_shows').value = 'last';
  cameraShowsChanged('t');
  check(saved().camera_shows === 'last' && $('t_camera_entity').value === 'camera.door', 'last chosen starts on the first');

  // Removing only in the details, the choice follows the list.
  cameraShowsChanged('t');
  click('[data-camera-remove="1"]');
  check(rows().length === 1, 'removed');

  // Quelle: picking the radar is enough, the default color comes with it, rows stay apart.
  $('t_camera_source').value = 'r';
  cameraSourceChanged('t');
  check(rows().length === 1 && saved().camera_entries.startsWith('r\\tzone.home'), 'the radar starts at home');
  check($('t_camera_entity').value === 'camera.ht_radar_home', 'its entity');
  check($('t_tile_icon').value === 'weather-pouring', 'its icon on the tile');
  check($('t_tile_icon_color').value === '#29B6F6', 'blue by default');
  check($('t_camera_list_label').textContent === 'Places' && $('t_camera_add').textContent === 'Add place', 'its words');

  // Own coordinates: the fields appear only then, comma or dot, errors in words.
  click('[data-camera-add]');
  click('[data-camera-toggle="1"]');
  pick('zone.arbeit');
  check(!$('t_camera_entry_lat'), 'no coordinate fields for a zone');
  check(saved().camera_entries.includes('r\\tzone.arbeit'), 'a zone');
  pick('geo:own');
  check($('t_camera_entry_lat') && rows()[1].textContent.includes('No coordinates yet'), 'the fields appear');
  type('t_camera_entry_lat', '95');
  check($('t_camera_entry_coord_fail').textContent === 'Latitude: from -90 to 90.' && !$('t_camera_entry_coord_fail').hidden, 'out of range');
  type('t_camera_entry_lat', '47,58');
  type('t_camera_entry_lon', '12.17');
  check($('t_camera_entry_coord_fail').hidden, 'a comma counts');
  check(saved().camera_entries.includes('c\\tgeo:47.58,12.17') && rows()[1].textContent.includes('47.58, 12.17'), 'the coordinates');
  // A found town.
  pick('geo:46.0664,11.1242');
  check(saved().camera_entries.includes('r\\tgeo:46.0664,11.1242') && rows()[1].textContent.includes('Trentino, Italia'), 'a town');

  // Webcam: an address names the row by its host; each source keeps its rows.
  $('t_camera_source').value = 'w';
  cameraSourceChanged('t');
  check(rows().length === 0 && $('t_camera_add').textContent === 'Add webcam', 'webcams have their own list');
  click('[data-camera-add]');
  check(rows()[0].textContent.includes('New webcam'), 'its placeholder');
  click('[data-camera-toggle="0"]');
  type('t_camera_entry_url', 'https://www.bergfex.at/abtenau/webcams/c1679/');
  check(rows()[0].textContent.includes('bergfex.at'), 'the host names it');
  check($('t_camera_entry_title').value === 'bergfex.at', 'and its automatic title');
  check($('t_camera_entity').value.startsWith('camera.ht_cam_'), 'its entity');
  check($('t_tile_icon').value === 'webcam', 'its icon');
  $('t_camera_source').value = 'h';
  cameraSourceChanged('t');
  check($('t_tile_icon_color').dataset.unset === '1', 'the default color goes with the source');
  check(rows().length === 1 && saved().camera_source === 'h', 'back to the first list');

  // A draft or paste brings everything back.
  const draft = saved();
  $('t_camera_source').value = 'h';
  resetCameraFields('t');
  check(rows().length === 0 && $('t_camera_entity').value === '', 'reset');
  loadCameraFields('t', Object.assign({}, draft, {camera_display: '1', camera_fit: 'fill', camera_head: 'auto', camera_tap: '0', camera_every: '10', camera_transparency: '0'}));
  check(rows().length === 1 && $('t_camera_entity').value === 'camera.door', 'a draft loads');

  // Stored fields of GET /api/tiles: the shown entry and last chosen.
  loadCameraFields('t', {sensor_entity: 'camera.ht_radar_zone_arbeit', sensor_display_mode: 0, key_modifier: 0,
    camera_entries: 'r\\tzone.home\\t\\t\\tHome\\tHA\\nr\\tzone.arbeit\\t\\t\\tArbeit\\tHA'});
  check($('t_camera_source').value === 'r' && $('t_camera_shows').value === '1', 'the shown entry');
  loadCameraFields('t', {sensor_entity: 'camera.ht_radar_home', sensor_display_mode: 0, key_modifier: 8,
    camera_entries: 'r\\tzone.home\\t\\t\\tHome\\tHA\\nr\\tzone.arbeit\\t\\t\\tArbeit\\tHA'});
  check($('t_camera_shows').value === 'last', 'last chosen');
  document.body.dataset.result = 'pass';
} catch (error) {
  document.body.dataset.result = 'fail';
  document.body.dataset.error = String(error && error.stack || error);
}
`)}</script></body></html>`;

runDomHarness({label: 'Camera entries editor', html: harness, tmpPrefix: 'camera-entries-'});
console.log('Camera entries editor: PASS');
