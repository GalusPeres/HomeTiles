// A tinted screensaver tile keeps its opacity in the preview (user
// 2026-10-02: with Tile color "From icon" the preview card was opaque while
// the panel showed the wallpaper through it). The panel sets bg_opa after the
// tint (image_screensaver build_slot_tile); every preview tint must too.
import assert from 'node:assert/strict';
import vm from 'node:vm';
import {extractFunction, readRepoFile} from '../../lib/admin-source.mjs';

const read = p => readRepoFile(p).replace(/\r\n/g, '\n');
const iconColors = read('src/web/admin/tiles/icon-colors.js');
const context = {
  document: {documentElement: {}},
  getComputedStyle: () => ({getPropertyValue: () => '#1A1A1A'}),
  normalizeIconColorHex: value => /^#[0-9a-f]{6}$/i.test(String(value)) ? String(value).toUpperCase() : '',
};
vm.createContext(context);
vm.runInContext(extractFunction('tileTintBackground', iconColors) + '\n' +
  extractFunction('setTileTintBackground', iconColors), context);
const card = (opacity) => ({style: {}, dataset: opacity === undefined ? {} : {bgOpacity: String(opacity)}});

const opaque = card();
context.setTileTintBackground(opaque, '#FFC107', 20);
assert.match(opaque.style.background, /^#[0-9A-F]{6}$/, 'a grid tile stays opaque');
const seeThrough = card(128);
context.setTileTintBackground(seeThrough, '#FFC107', 20);
assert.equal(seeThrough.style.background, opaque.style.background + '80', 'a screensaver tile keeps its opacity');
const clear = card(0);
context.setTileTintBackground(clear, '#FFC107', 20);
assert.ok(clear.style.background.endsWith('00'), 'a clear screensaver tile stays clear');

// Every tint goes through it; the screensaver branches record the opacity.
for (const file of ['src/web/admin/tiles/grid-preview.js', 'src/web/admin/tiles/live-preview.js', 'src/types/media/admin.js']) {
  assert.doesNotMatch(read(file), /style\.background = tileTintBackground\(/, file + ' sets an opaque tint');
}
assert.match(read('src/web/admin/tiles/grid-preview.js'),
  /tileBgToHex\(tile\.bg_color, meta\.defaultBg \|\| '#353535'\), opacity\);\n\s*el\.dataset\.bgOpacity = String\(opacity\);/);
assert.match(read('src/web/admin/tiles/live-preview.js'),
  /isDefaultBg \? defaultBg : \(color \|\| defaultBg\), opacity\);\n\s*tileElem\.dataset\.bgOpacity = String\(opacity\);/);
assert.match(read('src/web/admin/tiles/live-preview.js'), /tileElem\.style\.background = '';\n\s*delete tileElem\.dataset\.bgOpacity;/);
// The slider shows no text-field ring when clicked (it drew a box around it).
const css = read('src/web/assets/admin.css');
assert.match(css, /input:not\(\[type="checkbox"\]\):not\(\[type="radio"\]\):not\(\[type="range"\]\):focus,/);
console.log('Screensaver tile tints keep the tile opacity in the preview; sliders without a field ring');
