// The tile grid is the primary control of the Web Admin. It used to be a set of
// plain divs with an onclick handler, so it could not be reached or operated
// without a pointer. This test pins the keyboard contract and the accessible
// names on both the server-rendered grid and the browser preview updates.
import assert from 'node:assert/strict';

import {
  adminSource, extractDeliveredFunction, inlineScriptSafe, readRepoFile
} from '../../lib/admin-source.mjs';
import {runDomHarness} from '../../lib/headless-dom.mjs';

const gridHtml = readRepoFile("src/web/server/render/web_admin_html.cpp");
const css = readRepoFile('src', 'web', 'assets', 'admin.css');
const registry = readRepoFile('src', 'types', 'types_registry.cpp');
const registryHeader = readRepoFile('src', 'types', 'types_registry.h');

// The server must emit every grid tile as a real, focusable button with a name.
for (const marker of [
  'html += "\\" role=\\"button\\" tabindex=\\"0\\" aria-label=\\"";',
  'get_tile_type_localized_label(tile.type)'
]) {
  assert.ok(gridHtml.includes(marker),
    `Server-rendered grid tiles must stay keyboard reachable: ${marker}`);
}
assert.ok(registryHeader.includes('const char* get_tile_type_localized_label('),
  'The localized type label must be shared, not duplicated per caller');
assert.ok(registry.includes('return entry ? localized_tile_type_label(*entry) : ""'),
  'get_tile_type_localized_label must reuse the single label switch');

// A clipped tile cannot show an outline, so the focus indicator has to be inset.
assert.match(css, /\.tile:focus-visible \{[^}]*box-shadow:inset[^}]*\}/,
  'Grid tiles need a visible focus indicator that survives the clip path');
assert.match(
  css,
  /\.tile\.active:focus-visible,\s*\.tile\[data-selected="1"\]:focus-visible\s*\{[^}]*box-shadow:[^}]*inset[^}]*!important[^}]*\}/,
  'Selected grid tiles must retain a distinct focus ring');
assert.match(css, /@media \(prefers-reduced-motion: reduce\)/,
  'The Admin styles must honour the reduced-motion preference');

// The browser side must answer Enter and Space and keep the name in step.
for (const marker of [
  'function enableTileKeys(tab)',
  'applyTileAriaLabel(tileElem, displayTitle, type)',
  'applyTileAriaLabel(el, displayTitle, typeValue)',
  "applyTileAriaLabel(tileElem, '', type)",
  "applyTileAriaLabel(el, '', typeValue)"
]) {
  assert.ok(adminSource.includes(marker),
    `Admin JavaScript must keep the tile keyboard contract: ${marker}`);
}

const productionFunctions = [
  extractDeliveredFunction('enableTileKeys'),
  extractDeliveredFunction('applyTileAriaLabel'),
  extractDeliveredFunction('isEditablePreview'),
  extractDeliveredFunction('editablePreviewText'),
  extractDeliveredFunction('updateTilePreview'),
  extractDeliveredFunction('renderTileFromData')
].join('\n\n');

const harness = `<!doctype html><html><body>
  <input id="folder0_tile_title" value="">
  <input id="folder0_tile_color" value="#353535">
  <input id="folder0_tile_type" value="0">
  <div id="tab-tiles-folder0">
    <div id="mainGrid" class="tile-grid">
      <div id="folder0-tile-0" class="tile sensor" role="button" tabindex="0"
           data-index="0" data-type="1"
           aria-label="Living room"><span id="child">Living room</span></div>
      <div id="folder0-tile-1" class="tile empty" role="button" tabindex="0"
           data-index="1" data-type="0"
           aria-label="Empty"></div>
    </div>
  </div>
  <pre id="result">running</pre>
  <script>
  (() => {
    const selected = [];
    let currentTileIndex = 0;
    let currentTileTab = 'folder0';
    const HIDDEN_SETTINGS_TILE_INDEX = -2;
    const sensorMetaCache = {icons: {}, names: {}, units: {}, values: {}};
    function selectTile(index, tab) { selected.push(tab + ':' + index); }
    function getTileGrid() { return document.getElementById('mainGrid'); }
    function getTileTypeMeta(value) {
      return String(value) === '0'
        ? {label: 'Empty', css: 'empty'}
        : {label: 'Sensor', css: 'sensor'};
    }
    function getSensorValueFontClass() { return ''; }
    function resolveIconName() { return ''; }
    function updateLayoutFromInputs() {}
    function isScreensaverTileTab() { return false; }
    ${inlineScriptSafe(productionFunctions)}
    function press(target, key) {
      target.dispatchEvent(new KeyboardEvent('keydown',
        {key, bubbles: true, cancelable: true}));
    }
    try {
      const grid = getTileGrid();
      const tiles = grid.querySelectorAll('.tile');
      enableTileKeys('folder0');
      // Binding twice must not stack a second listener.
      enableTileKeys('folder0');

      press(tiles[0], 'Enter');
      press(tiles[1], ' ');
      if (selected.join('|') !== 'folder0:0|folder0:1') {
        throw new Error('Enter and Space must select exactly once: ' +
          selected.join('|'));
      }

      // Space must not also scroll the page.
      const spaceEvent = new KeyboardEvent('keydown',
        {key: ' ', bubbles: true, cancelable: true});
      tiles[0].dispatchEvent(spaceEvent);
      if (!spaceEvent.defaultPrevented) {
        throw new Error('Space on a tile must be consumed');
      }

      // Unrelated keys and events from outside the grid are ignored.
      const before = selected.length;
      press(tiles[0], 'a');
      press(document.getElementById('result'), 'Enter');
      if (selected.length !== before) {
        throw new Error('Only Enter/Space on a grid tile may select');
      }

      // A child element inside a tile still resolves to its tile.
      press(document.getElementById('child'), 'Enter');
      if (selected[selected.length - 1] !== 'folder0:0') {
        throw new Error('A press inside a tile must select that tile');
      }

      // Accessible names follow the rendered content, with the localized type
      // name as the fallback for a tile without a title.
      applyTileAriaLabel(tiles[0], 'Kitchen', '1');
      if (tiles[0].getAttribute('aria-label') !== 'Kitchen') {
        throw new Error('A title must become the accessible name');
      }
      applyTileAriaLabel(tiles[0], '   ', '1');
      if (tiles[0].getAttribute('aria-label') !== 'Sensor') {
        throw new Error('A blank title must fall back to the type name');
      }
      applyTileAriaLabel(tiles[1], '', '0');
      if (tiles[1].getAttribute('aria-label') !== 'Empty') {
        throw new Error('An empty tile must still be announced');
      }

      // Both rendering paths reuse existing tile elements. Deleting a tile
      // must replace the previous accessible name instead of leaving it stale.
      updateTilePreview('folder0');
      if (tiles[0].getAttribute('aria-label') !== 'Empty') {
        throw new Error('The live preview kept the deleted tile name');
      }
      tiles[1].setAttribute('aria-label', 'Old cached title');
      renderTileFromData('folder0', 1, {type: 0}, sensorMetaCache);
      if (tiles[1].getAttribute('aria-label') !== 'Empty') {
        throw new Error('The cached grid kept the deleted tile name');
      }

      document.body.dataset.result = 'pass';
      document.getElementById('result').textContent = 'pass';
    } catch (error) {
      document.body.dataset.result = 'fail';
      document.getElementById('result').textContent = error.stack || String(error);
    }
  })();
  </script>
</body></html>`;

if (!runDomHarness({
  label: 'Admin tile keyboard DOM test',
  html: harness,
  tmpPrefix: 'hometiles-tile-keyboard-'
})) {
  console.log('Admin tile keyboard source contract passed.');
}
