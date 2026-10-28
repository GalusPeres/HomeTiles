// A rule row's own "Fuellen" in the Web Admin preview (user 2026-10-11:
// "Ein gefuellt, Aus nicht gefuellt", also for a Folder on another entity):
// the matching row decides the fill like the panel (tile_icon_disc
// shown_fill), the light theme lets such a tile change between the white and
// the filled look (tile_surface_rule_of), and a row without "Fuellen" keeps
// even a tile with "Ausfuellen" plain. Runs the delivered preview functions.
import assert from 'node:assert/strict';
import vm from 'node:vm';
import {extractDeliveredFunction} from '../../lib/admin-source.mjs';

const context = vm.createContext({
  String, Number, Math, Array, Object, JSON, TextEncoder, TextDecoder,
  ICON_FILL_DEFAULT: 20,
  KEEPS_OWN_COLOR_TYPES: ['9', '10', '16'],
  ICON_COLOR_MAX_ROWS: 6,
  ICON_COLOR_MAX_STOPS: 6,
  ICON_COLOR_MAX_VALUE_BYTES: 32,
  ICON_COLOR_MAX_NUMBER_BYTES: 12,
  ICON_COLOR_LEGACY_MAX_RULES: 3,
  light: true,
  panelThemeLight: () => context.light,
  isScreensaverTileTab: tab => tab === 'screensaver',
  tileBgFollowsDefault: () => true,
});
vm.runInContext(['iconColorTrim', 'iconColorParseHex', 'iconColorValidIcon', 'iconColorParseHexIcon',
  'iconColorIconSuffix', 'iconColorHex', 'iconColorIsV2', 'iconColorFold',
  'iconColorParseDecimal', 'iconColorLeadingNumber', 'iconColorParseBar', 'iconColorParseStop', 'iconColorSortStops',
  'iconColorMix', 'iconColorBarColorAt', 'iconColorBarColor', 'iconColorBarIcon',
  'iconColorTextMatches', 'iconColorParseRule', 'iconColorRowsFill', 'resolveIconColorRecord', 'iconColorFillOf',
  'iconColorValidEntity', 'iconColorParseSource', 'iconColorRecordSource', 'iconColorClipValue', 'iconColorLegacyNumericValid',
  'iconColorMigrateLegacyBar', 'iconColorCoverOf', 'normalizeIconColorRecord', 'parseIconColorRecord',
  'tileFillOf', 'tileTypeKeepsOwnColor', 'tileFills', 'tileBgValueIsSet', 'tileHasOwnColor', 'tileFollowsTint',
  'setPreviewRuleFill', 'applyTileRulesTint'].map(extractDeliveredFunction).join('\n'), context);

const binary = 'v2\n\nis+ FFC107 on\nis- 9E9E9E off';
const element = () => ({dataset: {}, style: {background: '#FFFFFF'}});
const resolve = (record, state) => {
  const out = {};
  const color = context.resolveIconColorRecord(record, state, null, true, out);
  return {color, out};
};

// The row the state matches reports its own fill.
assert.deepEqual([resolve(binary, 'on').out.fill, resolve(binary, 'off').out.fill, resolve(binary, 'x').out.fill], [1, 0, -1]);
assert.equal(resolve('v2\n\nis FFC107 on', 'on').out.fill, -1, 'a row from before b382 follows Ausfuellen');

// Desk without "Ausfuellen": on fills at the default strength, off stays plain.
for (const [state, fill] of [['on', '20'], ['off', undefined], ['unknown', undefined]]) {
  const el = element();
  context.setPreviewRuleFill(el, resolve(binary, state).out);
  context.applyTileRulesTint(el, '20', binary, 'binary_sensor.desk', null);
  assert.equal(el.dataset.iconFill, fill, `Desk ${state}`);
}
// With "Ausfuellen": a row without "Fuellen" keeps the tile plain, also
// without the idle grey of a tile filled from the start.
const filled = 'v2\n\nfill 20\nis+ FFC107 on\nis- 9E9E9E off';
{
  const el = element();
  el.dataset.neutralIdle = '1';
  context.setPreviewRuleFill(el, resolve(filled, 'off').out);
  context.applyTileRulesTint(el, '20', filled, 'binary_sensor.desk', null);
  assert.equal(el.dataset.iconFill, undefined, 'off: no fill');
  assert.equal(el.dataset.neutralIdle, undefined, 'off: no idle grey');
}
{
  const el = element();
  el.dataset.neutralIdle = '1';
  context.setPreviewRuleFill(el, resolve(filled, 'x').out);
  context.applyTileRulesTint(el, '20', filled, 'binary_sensor.desk', null);
  assert.equal(el.dataset.iconFill, '20', 'no deciding row: Ausfuellen');
  assert.equal(el.dataset.neutralIdle, '1', 'no deciding row: the idle grey stays');
}

// The light theme: a row with "Fuellen" lets the tile change its look, on
// the page and never on the screensaver; the dark theme needs no surface.
assert.equal(context.tileFollowsTint('folder1', 'navigate', 0, 'v2\n\nsrc rules light.desk\nis+ FFC107 on', '4'), true,
  'Folder Lighting on the desk lamp');
assert.equal(context.tileFollowsTint('folder1', 'binary_sensor', 0, 'v2\n\nis- 9E9E9E off', '20'), false,
  'only rows without Fuellen: the white card');
assert.equal(context.tileFollowsTint('screensaver', 'binary_sensor', 0, binary, '20'), false, 'not on the screensaver');
context.light = false;
assert.equal(context.tileFollowsTint('folder1', 'binary_sensor', 0, binary, '20'), false, 'dark theme');

console.log('Rule row fill preview: the matching row fills or keeps the tile plain, like the panel');
