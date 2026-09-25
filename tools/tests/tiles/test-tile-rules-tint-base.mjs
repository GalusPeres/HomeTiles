// The Web Admin preview tints a tile with "Tint tile" rules like
// tile_icon_source.cpp does on the device: the tint replaces the tile color
// and always starts from the global default tile color, so an own tile color
// (Use global tile color off) never mixes with the rule color. Runs the real
// applyTileRulesTint from grid-preview.js.
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
  document: {addEventListener() {}, getElementById: () => null, documentElement: {}},
  TextEncoder, TextDecoder, Number, Math, String, Array,
});
vm.runInContext(read('src/web/admin/tiles/icon-colors.js'), context);
vm.runInContext(extractFunction(read('src/web/admin/tiles/grid-preview.js'), 'applyTileRulesTint'), context);
// The rules resolve to a blue tint at 20 %; the entity state is not under test.
context.iconColorTilePreviewTint = () => ({color: '#2196F3', percent: 20});

function tint(ownBackground, globalColor) {
  const el = {style: {background: ''}};
  context.getComputedStyle = target => target === el
    ? {backgroundColor: ownBackground}
    : {getPropertyValue: name => (name === '--tile-default-bg' ? globalColor : '')};
  context.applyTileRulesTint(el, '12', 'v2\n\nsrc auto self tile=20', 'climate.test', {});
  return el.style.background;
}

const expected = context.tileTintBackground('#222222', '#2196F3', 20);
assert.equal(tint('rgb(139, 0, 0)', ' #222222'), expected, 'An own tile color is replaced, not mixed');
assert.equal(tint('rgb(34, 34, 34)', '#222222'), expected, 'Global tiles tint the same');
assert.equal(tint('rgb(139, 0, 0)', '#3A3A3A'), context.tileTintBackground('#3A3A3A', '#2196F3', 20),
  'The global default tile color is the base');
assert.equal(tint('rgb(139, 0, 0)', ''), expected, 'Without the variable the built-in default is the base');

// Firmware: the same base.
assert.ok(read('src/tiles/runtime/tile_icon_source.cpp')
  .includes('const uint32_t tint = tile_tint::background(tileDefaultBgColor(), color, percent);'));
console.log('Tile rules tint replaces the tile color from the global base');
