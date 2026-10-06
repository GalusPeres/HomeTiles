// Tile tab layout on wide windows: the grid preview and the Settings parking
// stay in place and only the footer with Global settings scrolls (its
// scrollbar sits beside it, not right of the Tile settings panel); the tile
// editing hint sits beside the Settings parking on the root grid without
// widening the column (test-tile-color-custom-grey.mjs covers the picker).
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';
import {runDomHarness} from '../../lib/headless-dom.mjs';

const css = readRepoFile('src/web/assets/admin.css');
const wide = /@media \(min-width: 1181px\) \{([\s\S]*?)\n    \}\n/.exec(css);
assert.ok(wide, 'wide windows have their own tile tab layout');
const rules = wide[1];
assert.match(rules, /\.tab-content\.tile-tab\.active \{ display:flex; flex-direction:column; \}/,
  'the tile tab lays out preview, parking and footer as a column');
assert.match(rules, /\.tile-grid-scroll,\s*\.tab-content\.tile-tab\.active \.settings-hidden-parking \{ flex:0 0 auto; \}/,
  'preview and parking keep their size');
assert.match(rules, /\.folder-footer \{\s*flex:1 1 auto;\s*min-height:90px;\s*overflow-y:auto;/,
  'the Global settings footer scrolls and keeps a usable height');
// The Tile settings panel takes the row height, also for a short type
// (test-tile-settings-full-height.mjs); no script caps it inline (a cap from
// the window height ran 2 px past the row and scrolled the tab).
assert.match(rules, /\.tab-content\.tile-tab\.active \.tile-settings \{ align-self:stretch; max-height:100%; \}/);
assert.match(readRepoFile('src/web/admin/navigation/tabs.js'),
  /function updateTileSettingsMaxHeight\(\) \{\s*document\.querySelectorAll\('\.tile-settings'\)\.forEach\(panel => \{ panel\.style\.maxHeight = ''; \}\);\s*\}/);
// The parking row follows the grid width, so its hint wraps instead of
// widening the column past a narrow (480x480) grid and pushing the Tile
// settings panel out of the card.
assert.match(css, /\.settings-hidden-parking \{\s*display:flex;\s*align-items:center;\s*gap:14px;\s*width:0;\s*min-width:100%;\s*\}/);
// Narrower windows keep the scrolling tab (the settings panel moves below).
assert.match(css, /\.tab-content\.tile-tab\.active \{\s*flex:1 1 auto;\s*min-height:0;\s*overflow-x:hidden;\s*overflow-y:auto;/);

const html = readRepoFile('src/web/server/render/web_admin_html.cpp');
const tab = cppFunctionDefinitions(html).find((f) => f.name === 'appendTileTabHTML');
assert.ok(tab, 'appendTileTabHTML');
assert.match(tab.source, /<div class=\\"settings-parking-texts\\"><div id=\\"settingsHiddenHint\\"[\s\S]{0,260}html \+= "<\/div><p class=\\"hint\\">";\s*html \+= tr\.admin_tile_hint;/,
  'the tile editing hint sits beside the Settings parking');
// A folder shows the hint and Delete Folder where Home keeps its parking
// slot; no footer carries them.
assert.match(tab.source, /\} else if \(!screensaver_mode\) \{[\s\S]{0,400}settings-hidden-parking folder-side[\s\S]{0,200}html \+= tr\.admin_tile_hint;[\s\S]{0,200}btn-delete-folder[\s\S]{0,200}tr\.admin_delete_folder_tab;/,
  'folders show the hint and Delete Folder beside or below the grid');
assert.equal((tab.source.match(/tr\.admin_tile_hint/g) || []).length, 2,
  'the hint appears once for Home and once for folders, never in a footer');
assert.equal((tab.source.match(/btn-delete-folder/g) || []).length, 1, 'one Delete Folder button');

// Real layout on the smallest grid (S3 480x480, 4x4 cells of 116 px): the
// parking row is only as wide as the grid, so the long hint must wrap beside
// the slot instead of squeezing it (regression: the slot shrank to about half
// a cell and the Settings tile covered the hint).
const cells = '<div class="tile"></div>'.repeat(16);
const parkingHtml = (slotState, hiddenHint, wide) => `<!doctype html><html><head><style>${css}
:root{--grid-cols:4;--grid-rows:4;--preview-cell-w:116px;--preview-cell-h:116px;--preview-gap:14px;--preview-pad:14px}</style></head>
<body><div class="wrapper"><div class="card"><div class="tab-content tile-tab active"><div class="tile-editor"><div class="tile-editor-main" id="main">
<div class="tile-grid-scroll"><div class="tile-grid" id="grid">${cells}</div></div>
<div class="settings-hidden-parking"><div id="slot" class="tile-grid settings-hidden-slot ${slotState}"><div id="tile" class="tile settings-hidden-tile"></div></div>
<div class="settings-parking-texts" id="texts"><div class="settings-hidden-hint ${hiddenHint}">Settings hier ablegen, um sie auszublenden</div>
<p class="hint">Klicke auf eine Kachel, um sie zu bearbeiten. Kacheln lassen sich per Drag &amp; Drop verschieben und am Eckgriff vergrößern oder verkleinern. Wähle den Typ und passe die Einstellungen an.</p></div></div>
<div class="folder-footer"></div></div><div class="tile-settings"></div></div></div></div></div>
<pre id="result"></pre><script>
const box = (id) => document.getElementById(id).getBoundingClientRect();
const slot = box('slot'), tile = box('tile'), texts = box('texts'), grid = box('grid'), main = box('main');
const problems = [];
if (tile.width < 115) problems.push('tile ' + tile.width);
if (slot.width < tile.width) problems.push('slot ' + slot.width + ' narrower than its tile ' + tile.width);
if (texts.left < slot.right) problems.push('hint starts at ' + texts.left + ' under the slot ending at ' + slot.right);
if (texts.right > main.right + 1) problems.push('hint ends at ' + texts.right + ' past its column at ' + main.right);
// Wide windows: the column stays as wide as the grid, so the Tile settings
// panel beside it keeps its room.
if (${wide} && main.right > grid.right + 1) problems.push('column ends at ' + main.right + ' past the grid at ' + grid.right);
document.body.dataset.result = problems.length ? 'fail' : 'pass';
document.getElementById('result').textContent = problems.join('; ');
</script></body></html>`;
for (const width of [1500, 1000]) {
  for (const [slotState, hiddenHint] of [['has-tile', 'is-hidden'], ['', '']]) {
    runDomHarness({
      label: `Settings parking at ${width} px (${slotState || 'empty slot'})`,
      html: parkingHtml(slotState, hiddenHint, width >= 1181),
      tmpPrefix: 'hometiles-parking-',
      extraArgs: [`--window-size=${width},1100`]
    });
  }
}

console.log('Tile tab: fixed preview, scrolling Global settings, hint beside the parking');
