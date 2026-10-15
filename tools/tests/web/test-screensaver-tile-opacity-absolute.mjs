// A screensaver tile's Opacity is the real opacity of its card, like the
// clock card's (user 2026-10-15): 50 % draws at 50 %, not 50 % of the global
// fill opacity. A tile left at its default (255) takes the global value. The
// panel (screensaver_tile_look::fill_opacity) and both Web Admin previews
// (screensaverTileFillOpacity) give the same numbers.
import assert from 'node:assert/strict';
import vm from 'node:vm';
import {readRepoFile} from '../../lib/admin-source.mjs';

const read = p => readRepoFile(p).replace(/\r\n/g, '\n');

// Panel rule, evaluated from the header's one-line body.
const look = read('src/ui/screensaver/screensaver_tile_look.h');
const body = look.match(/inline uint8_t fill_opacity\(uint8_t global_opacity, const Tile& tile\) \{\n\s*return ([^;]+);/);
assert.ok(body, 'fill_opacity body');
const panel = (global, own) => vm.runInNewContext(body[1], {global_opacity: global, tile: {ss_opacity: own}});

// Browser rule, run from the real editor source.
const editor = read('src/web/admin/screensaver/editor.js');
const start = editor.indexOf('function screensaverTileFillOpacity(');
const end = editor.indexOf('\n  }\n', start);
assert.ok(start > 0 && end > start, 'screensaverTileFillOpacity');
const browser = (global, own) => vm.runInNewContext(
  editor.slice(start, end + 4) + '\nscreensaverTileFillOpacity(own);',
  {own, ssClamp: (v, lo, hi) => Math.min(hi, Math.max(lo, Number(v))),
   screensaverTileOpacity: () => global});

for (const [global, own, expected] of [
  [217, 128, 128],  // 50 % stays 50 % under the default 85 % global
  [217, 255, 217],  // default follows the global fill opacity
  [64, 200, 200],   // an own value is never scaled
  [217, 0, 0],      // 0 stays see-through
]) {
  assert.equal(panel(global, own), expected, `panel ${global}/${own}`);
  assert.equal(browser(global, own), expected, `browser ${global}/${own}`);
}
assert.equal(browser(217, undefined), 217, 'a missing value is the default');
console.log('Screensaver tile opacity: own value absolute, default follows the global fill opacity');
