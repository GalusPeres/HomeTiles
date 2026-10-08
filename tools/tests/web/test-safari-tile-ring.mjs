// Issue #80: Safari has no overflow-clip-margin, so the tile's overflow cut
// the selection and hover rings (::after on the 3 px editor border) of every
// tile from 1 x 1; only half tiles (no border, ring inside) showed them.
// Without the clip margin the border itself is the ring.
import assert from 'node:assert/strict';
import fs from 'node:fs';

const css = fs.readFileSync(new URL('../../../src/web/assets/admin.css', import.meta.url), 'utf8');

// The rings lie on the border and need the clip margin.
assert.match(css, /@supports \(overflow-clip-margin:3px\) \{\s*\.tile \{ overflow:clip; overflow-clip-margin:3px; \}/);
assert.match(css, /\.tile:not\(\.empty\):is\(\.active, \[data-selected="1"\], :hover\)::after \{[^}]*inset:-3px;/);

const block = css.match(/@supports not \(overflow-clip-margin:3px\) \{([\s\S]*?)\n    \}/);
assert.ok(block, 'a fallback for browsers without overflow-clip-margin');
const fallback = block[1];
assert.match(fallback, /\.tile:not\(\.empty\):not\(\.sensor-compact\):is\(\.active, \[data-selected="1"\]\) \{ border-color:#26a69a; \}/,
  'the selection colours the border like the ring (#26a69a)');
assert.match(fallback, /\.tile:not\(\.empty\):not\(\.sensor-compact\):hover:not\(\.active\):not\(\[data-selected="1"\]\) \{\s*border:3px dashed rgba\(38,166,154,0\.6\);/,
  'hover: the dashed ring on the border');
assert.match(fallback, /body\.tile-resize-active \.tile:not\(\.empty\):not\(\.sensor-compact\):hover:not\(\.active\):not\(\[data-selected="1"\]\) \{\s*border-color:transparent;/,
  'no hover ring while resizing');
// Half tiles keep their own ring inside (no border to colour).
assert.match(css, /\.tile\.sensor-compact:not\(\.empty\)::after \{ inset:0; \}/);
// The fallback comes after the rules it overrides.
assert.ok(css.indexOf(block[0]) > css.indexOf('.tile:hover:not(.active) {'), 'after the hover border reset');
assert.ok(css.indexOf(block[0]) > css.indexOf('.tile.active,\n    .tile[data-selected="1"] {'), 'after the selection border reset');
console.log('ok');
