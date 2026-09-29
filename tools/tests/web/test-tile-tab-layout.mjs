// Tile tab layout on wide windows: the grid preview and the Settings parking
// stay in place and only the footer with Global settings scrolls (its
// scrollbar sits beside it, not right of the Tile settings panel); the tile
// editing hint sits beside the Settings parking on the root grid; and a first
// "Custom" tile color opens its picker at the color field (regression: the
// picker opened in the top left corner of the page).
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

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
// The Tile settings panel takes the row height; no script caps it inline (a
// cap from the window height ran 2 px past the row and scrolled the tab).
assert.match(rules, /\.tab-content\.tile-tab\.active \.tile-settings \{ max-height:100%; \}/);
assert.match(readRepoFile('src/web/admin/navigation/tabs.js'),
  /function updateTileSettingsMaxHeight\(\) \{\s*document\.querySelectorAll\('\.tile-settings'\)\.forEach\(panel => \{ panel\.style\.maxHeight = ''; \}\);\s*\}/);
// Narrower windows keep the scrolling tab (the settings panel moves below).
assert.match(css, /\.tab-content\.tile-tab\.active \{\s*flex:1 1 auto;\s*min-height:0;\s*overflow-x:hidden;\s*overflow-y:auto;/);

const html = readRepoFile('src/web/server/render/web_admin_html.cpp');
const tab = cppFunctionDefinitions(html).find((f) => f.name === 'appendTileTabHTML');
assert.ok(tab, 'appendTileTabHTML');
assert.match(tab.source, /<div class=\\"settings-parking-texts\\"><div id=\\"settingsHiddenHint\\"[\s\S]{0,260}html \+= "<\/div><p class=\\"hint\\">";\s*html \+= tr\.admin_tile_hint;/,
  'the tile editing hint sits beside the Settings parking');
assert.match(tab.source, /\/\/ The root grid shows this hint beside the Settings parking slot\.\s*if \(folder_id != 0\) html \+= R"html\(\s*<p class="hint">\)html";/,
  'the root footer has no second copy of the hint');
assert.match(tab.source, /\} else if \(folder_id != 0\) \{\s*html \+= tr\.admin_tile_hint;/,
  'folders keep the hint in their footer');

const js = readRepoFile('src/web/admin/tiles/grid-preview.js');
assert.match(js, /requestAnimationFrame\(\(\) => \{\s*try \{\s*if \(input\.offsetParent && typeof input\.showPicker === 'function'\) input\.showPicker\(\);/,
  'the first Custom opens the picker after the color row is laid out');
assert.doesNotMatch(js, /\n      try \{\n        if \(typeof input\.showPicker === 'function'\) input\.showPicker\(\);/,
  'no synchronous picker while the color row is still hidden');

console.log('Tile tab: fixed preview, scrolling Global settings, hint beside the parking, picker at the field');
