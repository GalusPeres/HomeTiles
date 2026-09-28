// At 100 % zoom the Global settings under the grid preview ran past the card
// and the fixed-height page cut them off. The tile tabs now scroll inside the
// card like Settings; the Tile settings panel keeps its computed height and
// sticks to the top of the scrolling tab, so its actions stay visible.
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';

const css = readRepoFile('src/web/assets/admin.css');
const rule = /\.tab-content\.tile-tab\.active \{([^}]*)\}/.exec(css);
assert.ok(rule, 'the active tile tab has its own layout rule');
for (const declaration of ['flex:1 1 auto;', 'min-height:0;', 'overflow-y:auto;']) {
  assert.ok(rule[1].includes(declaration), `the tile tab uses ${declaration}`);
}
assert.match(css, /@media \(max-width: 780px\) \{[\s\S]*?\.tab-content\.tile-tab\.active \{ flex:none; overflow:visible; \}/,
  'on phones the page scrolls instead');
assert.match(css, /position:sticky;\s*top:0;\s*max-height:calc\(100vh - 40px\);/,
  'the Tile settings panel sticks to the top of the scrolling tab');

const js = readRepoFile('src/web/assets/admin.js');
assert.match(js, /function updateTileSettingsMaxHeight\(\)/, 'the panel height still follows the visible space');

console.log('Tile tabs scroll inside the card; the Tile settings panel keeps its size');
