// The Web Admin preview tints a tile with "Tint tile" rules on top of the
// tile's own background, like tile_icon_source.cpp does on the device. A
// custom tile color (Use global tile color off) must be the mixing base, not
// the default grey. Runs the real applyTileRulesTint from grid-preview.js.
import assert from 'node:assert/strict';
import vm from 'node:vm';
import {readRepoFile} from '../../lib/admin-source.mjs';

const read = file => readRepoFile(file).replace(/\r\n?/g, '\n');

function extractFunction(source, name) {
  const start = source.indexOf(`function ${name}(`);
  assert.ok(start >= 0, `${name} exists`);
  let depth = 0;
  for (let i = source.indexOf('{', start); i < source.length; i++) {
    if (source[i] === '{') depth++;
    else if (source[i] === '}' && --depth === 0) return source.slice(start, i + 1);
  }
  throw new Error(`${name} is not closed`);
}

const context = vm.createContext({
  document: {addEventListener() {}, getElementById: () => null},
  TextEncoder, TextDecoder, Number, Math, String, Array,
});
vm.runInContext(read('src/web/admin/tiles/icon-colors.js'), context);
vm.runInContext(extractFunction(read('src/web/admin/tiles/grid-preview.js'), 'applyTileRulesTint'), context);
// The rules resolve to a blue tint at 20 %; the entity state is not under test.
context.iconColorTilePreviewTint = () => ({color: '#2196F3', percent: 20});

function tint(computedBackground) {
  const el = {style: {background: ''}};
  context.getComputedStyle = () => ({backgroundColor: computedBackground});
  context.applyTileRulesTint(el, '12', 'v2\n\nsrc auto self tile=20', 'climate.test', {});
  return el.style.background;
}

const custom = tint('rgb(139, 0, 0)');
assert.equal(custom, context.tileTintBackground('#8B0000', '#2196F3', 20), 'Custom tile color is the base');
assert.notEqual(custom, context.tileTintBackground('#2A2A2A', '#2196F3', 20), 'Not the default grey');
assert.equal(tint('rgb(42, 42, 42)'), context.tileTintBackground('#2A2A2A', '#2196F3', 20), 'Global color base');
assert.equal(tint(''), context.tileTintBackground('#2A2A2A', '#2196F3', 20), 'Unknown background falls back to grey');
console.log('Tile rules tint mixes with the tile\'s own background');
