// Tile Settings: type options marked data-tile-place move to their place
// (user 2026-10-26, Power flow): "head" under Title and Icon, "top" at the
// start of the settings, "color" into Color; they return to their type
// fields when another type is shown, the Advanced caption then sees no
// option, and the shown type names the Auto title and icon of a tile without
// an entity of its own (only while it is shown).
import assert from 'node:assert/strict';
import {extractDeliveredFunction, readRepoFile} from '../../lib/admin-source.mjs';
import {runDomHarness} from '../../lib/headless-dom.mjs';

const html = readRepoFile('src/web/server/render/web_admin_html.cpp').replace(/\r\n/g, '\n');
const at = text => html.indexOf(text);
assert.ok(at('_tile_icon_picker" aria-haspopup="listbox"></button></div></div>') < at('_tile_head_slot"></div>\n</div>\n<div class="tile-settings-body">') &&
  at('<div class="tile-settings-body">') < at('_tile_top_slot"></div>') &&
  at('_tile_top_slot"></div>') < at('appendHtmlEscaped(html, tr.tile_group_color);') &&
  at('appendHtmlEscaped(html, tr.tile_group_color);') < at('_tile_color_slot"></div>') &&
  at('_tile_color_slot"></div>') < at('append_tile_icon_color_fixed_html(html, tab_id);'),
  'slots: under Title and Icon in the head, at the top of the body, right under the Color caption');
assert.ok(readRepoFile('src/web/admin/tiles/type-selection.js').includes('placeTileEntityField(tab);\n    placeTileTypeParts(tab);\n    syncTileAdvancedCaption(tab);'),
  'placed with every type change, before the Advanced caption looks');

const page = `<!doctype html><html><body>
<div id="tSettings">
<button id="t_tile_title_auto"></button><input type="hidden" id="t_tile_icon">
<div class="tile-place-slot" id="t_tile_head_slot"></div>
<div class="tile-place-slot" id="t_tile_top_slot"></div>
<div class="tile-settings-group">Color</div><div class="tile-place-slot" id="t_tile_color_slot"></div>
<div class="tile-settings-group" id="t_tile_group_advanced">Advanced</div><p hidden id="t_tile_size_note"></p>
<div class="type-fields show" id="t_flow_fields" data-auto-title="Leistungsfluss" data-auto-icon="lightning-bolt">
  <div class="tile-place" data-tile-place="head"><input type="checkbox" id="header"></div>
  <div class="tile-place" data-tile-place="top"><input type="hidden" id="pv"><input type="checkbox" id="invert"></div>
  <div class="tile-place" data-tile-place="color"><input type="checkbox" id="card"></div>
</div>
<div class="type-fields" id="t_sensor_fields"><select id="unit"></select></div>
</div>
<pre id="result"></pre><script>
let refreshed = 0;
function refreshTileIconButtons(tab) { if (tab === 't') refreshed++; }
${extractDeliveredFunction('placeTileTypeParts')}
${extractDeliveredFunction('syncTileAdvancedCaption')}
try {
  const check = (ok, message) => { if (!ok) throw Error(message); };
  const $ = id => document.getElementById(id);
  placeTileTypeParts('t');
  syncTileAdvancedCaption('t');
  check($('t_tile_head_slot').contains($('header')), 'header option under Title and Icon');
  check($('t_tile_top_slot').contains($('pv')) && $('t_tile_top_slot').contains($('invert')), 'sensors on top');
  check($('t_tile_color_slot').contains($('card')), 'card in Color');
  check(!$('t_flow_fields').children.length, 'nothing left in the type fields');
  check($('t_tile_group_advanced').classList.contains('hidden'), 'no Advanced caption for nothing');
  check($('t_tile_title_auto').dataset.autoTitle === 'Leistungsfluss' && $('t_tile_icon').dataset.autoIcon === 'lightning-bolt',
    'Auto names the type and its icon');
  check(refreshed === 1, 'the head redraws');
  // Again (a second type change to the same type): nothing doubles.
  placeTileTypeParts('t');
  check($('t_tile_top_slot').children.length === 1 && $('t_tile_head_slot').children.length === 1, 'placed once');
  // Another type: everything goes home in its order, Auto forgets the type.
  $('t_flow_fields').classList.remove('show');
  $('t_sensor_fields').classList.add('show');
  placeTileTypeParts('t');
  syncTileAdvancedCaption('t');
  const home = [...$('t_flow_fields').children].map(part => part.dataset.tilePlace).join(',');
  check(home === 'head,top,color', 'home again in order: ' + home);
  check(!$('t_tile_head_slot').children.length && !$('t_tile_top_slot').children.length && !$('t_tile_color_slot').children.length,
    'slots empty');
  check(!('autoTitle' in $('t_tile_title_auto').dataset) && !('autoIcon' in $('t_tile_icon').dataset), 'no type Auto left');
  check(!$('t_tile_group_advanced').classList.contains('hidden'), 'the other type has its options');
  // A fixed Auto title (Home's head) is not the type's to remove.
  $('t_tile_title_auto').dataset.autoTitle = 'Home';
  placeTileTypeParts('t');
  check($('t_tile_title_auto').dataset.autoTitle === 'Home', 'a fixed Auto title stays');
  document.body.dataset.result = 'pass';
} catch (error) { document.body.dataset.result = 'fail'; document.getElementById('result').textContent = error.message; }
</script></body></html>`;
try { runDomHarness({label: 'Tile type places', html: page, tmpPrefix: 'hometiles-tile-places-'}); }
catch (error) { throw Error(error.message.match(/<pre id="result">([\s\S]*?)<\/pre>/)?.[1] || error.message.slice(0, 300)); }
console.log('Tile Settings: type options in their places, back home with another type, Auto names the type');
