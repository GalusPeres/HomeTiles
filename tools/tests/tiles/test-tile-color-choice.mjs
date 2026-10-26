// One tile color rule (user 2026-10-11): the color always comes from the
// icon. A tile with "Fill" (tile_icon_colors::fills: "fill NN", or a former
// "From cover" / rule "Tint tile") is filled with the color its icon shows,
// at the fixed strength (kTintDefault). Rules only color the icon (a former
// "Tint tile" rule too, rule_colors_icon). Grey, white and almost white give
// no fill (tile_tint::choose, tile_icon_source without_white). The
// maintainer's Desk (Binary sensor) and Water (Sensor bar) tiles run through
// the firmware rule natively and through the real preview functions.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import vm from 'node:vm';
import {spawnSync} from 'node:child_process';
import {readRepoFile, repoRoot} from '../../lib/admin-source.mjs';

const read = file => readRepoFile(file).replace(/\r\n?/g, '\n');
const js = vm.createContext({
  document: {addEventListener() {}, getElementById: () => null, querySelector: () => null,
    querySelectorAll: () => [], documentElement: {lang: 'en'}},
  window: {}, navigator: {language: 'en'}, TextEncoder, TextDecoder, Number, Math, String, Array, JSON,
  normalizeMdiIconName: value => String(value || ''),
});
for (const file of ['src/web/admin/core/localization.js', 'src/types/switch/admin.js',
  'src/types/binary_sensor/admin-state.js', 'src/types/climate/admin-preview.js', 'src/types/cover/admin.js',
  'src/web/admin/tiles/icon-colors.js']) vm.runInContext(read(file), js, {filename: file});

// The real preview functions (grid-preview.js): the icon color, the fill and
// the no-color test.
const grid = read('src/web/admin/tiles/grid-preview.js');
const functionSource = (source, name) => {
  const start = source.indexOf(`function ${name}(`);
  assert.ok(start >= 0, name);
  let depth = 0;
  let end = source.indexOf('{', start);
  for (; end < source.length; end++) {
    if (source[end] === '{') depth++;
    else if (source[end] === '}' && --depth === 0) break;
  }
  return source.slice(start, end + 1);
};
js.tileTypeHasIconColors = () => true;
js.iconColorRuleState = (type, entity, meta, binaryState) =>
  binaryState ? {state: binaryState.state, display: binaryState.display} : {state: String(meta.values[entity]), display: null};
assert.ok(grid.includes('const ICON_FILL_DEFAULT = 20;'));
vm.runInContext('const ICON_FILL_DEFAULT = 20;\n' + ['previewIconColor', 'tileFillOf', 'toneToLinear', 'toneOklch', 'toneNoColor']
  .map(name => functionSource(grid, name)).join('\n'), js);

// [input, expected normalized record, expected fill_of]
const cases = [
  ['v2\nFF0000\nfill 25', 'v2\nFF0000\nfill 25', 25],
  ['v2\nFF0000\nfill 99', 'v2\nFF0000\nfill 50', 50],
  ['v2\nFF0000\nfill 3', 'v2\nFF0000\nfill 10', 10],
  ['v2\n\nfill 25', 'v2\n\nfill 25', 25],
  ['v2\nFF0000\nfill x', 'v2\nFF0000', 0],
  ['v2\nFF0000', 'v2\nFF0000', 0],
  ['v2\nFF0000\nsrc auto light.kitchen tile=30\nfill 20', 'v2\nFF0000\nfill 20\nsrc auto light.kitchen tile=30', 20],
];
for (const [input, expected, fill] of cases) {
  assert.equal(js.normalizeIconColorRecord(input, false, false, true, false), expected, `JS normalize ${JSON.stringify(input)}`);
  assert.equal(js.parseIconColorRecord(input).fill, fill, `JS fill ${JSON.stringify(input)}`);
}
// fills(): "fill", a former cover tint and a former rule tint; nothing else.
const fillRecords = [['v2\nFF0000\nfill 25', 20], ['v2\nFF0000', 0], ['v2\n\nsrc rules self tile=30\nhas F44336 6', 20],
  ['v2\n\nsrc rules self\nhas F44336 6', 0], ['v2\n\ncover icon tile=20', 20], ['v2\n\ncover icon', 0]];
for (const [record, fill] of fillRecords) assert.equal(js.tileFillOf(record), fill, `preview fills ${JSON.stringify(record)}`);

// Scenarios: [name, type, record, state, display, type color].
const desk = 'v2\nFFC107\nfill 20\nsrc rules self tile=20 noicon\nis 787161 on\nis A0A0A0 off';
const deskColorIcon = 'v2\nFFC107\nfill 20\nsrc rules self tile=20\nis 787161 on\nis A0A0A0 off';
const water = 'v2\nD63B3B\nsrc rules self tile=20 noicon\nbar smooth 20 70 0:3B82F6 180:22C55E 340:F59E0B 540:EF4444';
const scenarios = [
  ['Desk detected', '20', desk, 'on', 'Detected', '#FFC107'],
  ['Desk clear', '20', desk, 'off', 'Clear', '#9E9E9E'],
  ['Desk with Color icon, clear', '20', deskColorIcon, 'off', 'Clear', '#9E9E9E'],
  ['Desk with Color icon, detected', '20', deskColorIcon, 'on', 'Detected', '#FFC107'],
  ['Water 47.7', '1', water, '47.7', null, '#FFFFFF'],
  ['Water 25', '1', water, '25', null, '#FFFFFF'],
  ['Former tint rule applies', '1', 'v2\n\nsrc rules self tile=30\nhas F44336 6', 'in 6 Tagen', null, '#FFFFFF'],
  ['Former tint rule, no match', '1', 'v2\n\nsrc rules self tile=30\nhas F44336 6', 'in 5 Tagen', null, '#FFFFFF'],
  ['Fill, own icon color', '1', 'v2\n00BCD4\nfill 35', '12', null, '#FFFFFF'],
  ['Fill, white icon', '1', 'v2\n\nfill 35', '12', null, '#FFFFFF'],
  ['Fill, almost white icon', '1', 'v2\nFFFFFB\nfill 20', '12', null, '#FFFFFF'],
  ['No fill, own icon color', '1', 'v2\n00BCD4', '12', null, '#FFFFFF'],
  ['Fill with a former tint rule', '1', 'v2\nD63B3B\nfill 20\nsrc rules self tile=30 noicon\nbar smooth 20 70 0:3B82F6 540:EF4444', '47.7', null, '#FFFFFF'],
].map(([name, type, record, state, display, fallback]) =>
  [name, type, js.normalizeIconColorRecord(record, true, true, true, true), state, display, fallback]);
for (const scenario of scenarios) assert.ok(scenario[2].startsWith('v2\n'), `normalized ${scenario[0]}: ${scenario[2]}`);

const GLOBAL = '#1A1A1A';
const rgbOf = hex => [1, 3, 5].map(i => parseInt(hex.slice(i, i + 2), 16));
const preview = scenarios.map(([, type, record, state, display, fallback]) => {
  const entity = 'sensor.x';
  const meta = {values: {[entity]: state}};
  const icon = js.normalizeIconColorHex(js.previewIconColor(type, record, entity, meta,
    type === '20' ? {state, display} : null, fallback)) || fallback;
  // applyIconDiscTint: the fill follows the icon; no color never fills.
  let choice = js.tileTintChoice(false, '', 0, js.tileFillOf(record), icon);
  if (choice && js.toneNoColor(rgbOf(icon))) choice = null;
  const bg = choice ? js.tileTintBackground(GLOBAL, choice.color, choice.percent) : '-';
  return [icon, choice ? `${choice.color}@${choice.percent}` : '-', bg].join('|');
});
const expect = (name, predicate) => {
  const index = scenarios.findIndex(s => s[0] === name);
  assert.ok(predicate(preview[index].split('|')), `${name}: ${preview[index]}`);
};
// Desk: its rule colors the icon (also the former "Tint tile only" rule), the
// tile is filled with that color; the grey Clear color fills nothing.
expect('Desk detected', ([icon, choice]) => icon === '#787161' && choice === '#787161@20');
expect('Desk clear', ([icon, choice]) => icon === '#A0A0A0' && choice === '-');
expect('Desk with Color icon, clear', ([icon, choice]) => icon === '#A0A0A0' && choice === '-');
expect('Desk with Color icon, detected', ([icon, choice]) => icon === '#787161' && choice === '#787161@20');
// Water (a former "Tint tile" rule): the icon takes the bar color, the tile
// is filled with it.
expect('Water 47.7', ([icon, choice]) => icon === '#EF4444' && choice === '#EF4444@20');
expect('Water 25', ([icon, choice]) => icon !== '#D63B3B' && choice === icon + '@20');
expect('Former tint rule applies', ([icon, choice]) => icon === '#F44336' && choice === '#F44336@20');
expect('Former tint rule, no match', ([, choice]) => choice === '-');
expect('Fill, own icon color', ([icon, choice]) => icon === '#00BCD4' && choice === '#00BCD4@20');
expect('Fill, white icon', ([, choice]) => choice === '-');
expect('Fill, almost white icon', ([icon, choice]) => icon === '#FFFFFB' && choice === '-');
expect('No fill, own icon color', ([icon, choice]) => icon === '#00BCD4' && choice === '-');
expect('Fill with a former tint rule', ([icon, choice]) => icon === '#EF4444' && choice === '#EF4444@20');

// Preview: the fill goes through tileTintChoice and the no-color test.
for (const marker of [
  'if (fill) el.dataset.iconFill = String(fill);',
  "tileTintChoice(false, '', 0, fill, givenHex)",
  'if (choice && toneNoColor(given)) choice = null;',
  'setTileTintBackground(tileElem, choice.color, choice.percent);',
  'tileElem.style.background = tileElem.dataset.baseBg;',
  "if (input && input.dataset.unset === '1' && given) input.value = givenHex;",
]) assert.ok(grid.includes(marker), 'grid-preview: ' + marker);
assert.doesNotMatch(grid + read('src/web/admin/tiles/icon-colors.js') + read('src/types/media/admin.js'),
  /ruleTint|iconColorTilePreviewTint|iconColorSourceAutoActive/, 'No rule tint in the preview');

// Firmware: refresh_card and the icon color hook decide through
// tile_tint::choose and without_white; icon color changes reach the hook.
const source = read('src/tiles/runtime/tile_icon_source.cpp');
for (const marker of [
  'const bool rule_icon = colored && tile_icon_colors::rule_colors_icon(layer);',
  'set_icon_fill_marker(card, tileFills(tile) ? tile_icon_colors::kTintDefault : 0);',
  // One decision for refresh_card and the hook (show_fill): the fill the
  // card shows now, a rule row's own "Fuellen" first.
  'show_fill(card, find_disc(card));',
  'const uint8_t fill = tile_icon_disc::shown_fill(card);',
  'apply_tint_choice(card, with_white(card, without_white(tile_tint::choose(false, 0, 0, fill, disc_icon_rgb(disc))),',
  'if (choice.percent && tone_color::no_color(choice.color)) return tile_tint::Choice{};',
  'lv_obj_t* card = tile_icon_disc::fill_card_of(lv_obj_get_parent(disc));',
  'show_fill(card, disc);',
  'tile_icon_disc::g_icon_color_hook = &on_icon_color;',
]) assert.ok(source.includes(marker), 'tile_icon_source: ' + marker);
assert.ok(read('src/tiles/runtime/tile_icon_disc.h').includes('if (g_icon_color_hook) g_icon_color_hook(disc);'));
// Clock, Text and Pixel animation keep an own color and are never filled
// (one color control per tile), on the device and in the preview.
assert.ok(read('src/tiles/config/tile_config.h').includes(
  'return !tileTypeKeepsOwnColor(tile.type) && tile_icon_colors::fills(tile.icon_colors.c_str());'));
assert.ok(read('src/tiles/runtime/tile_renderer.cpp').includes(
  'return tile.type == TILE_SWITCH ? tile_surface::kFollowTint : tile_surface::kFollowTint | tile_surface::kNeutralIdle;'));
assert.ok(grid.includes('function tileFills(typeValue, iconColors) { return tileTypeKeepsOwnColor(typeValue) ? 0 : tileFillOf(iconColors); }') &&
  grid.includes('const ownFill = tileFills(typeValue, record);') &&
  grid.includes('const iconOffered = !keepsOwn && typeof tileTypeHasIconColors'));
const record = read('src/tiles/config/tile_icon_colors.h');
assert.ok(record.includes('return fill_of(record) > 0 || cover_of(record).tile > 0 || source_of(record).tile > 0;'));
assert.ok(record.includes('inline bool rule_colors_icon(const Source& layer) { return layer.icon || layer.tile > 0; }'));

// Editor: one "Fill" checkbox, the rules say that they win; no strengths.
const html = read('src/web/server/render/tile_icon_colors_html.cpp');
for (const marker of ['void append_tile_color_from_icon_html(String& html, const String& tab_id) {',
  '_tile_icon_fill" data-icon-color="fill-target"> )html";', 'appendHtmlEscaped(html, tr.tile_fill);',
  'appendHtmlEscaped(html, tr.tile_rules_priority_hint);']) assert.ok(html.includes(marker), marker);
assert.doesNotMatch(html, /fill-strength|strength-reset|_tile_icon_rule_tile|tile_rules_tile_follows_icon|tile_rules_tint_tile/,
  'No strength, no Tint tile');
const editor = read('src/web/admin/tiles/icon-colors.js');
for (const marker of ["if (iconColorEl(tab, '_tile_icon_fill')?.checked) lines.push('fill 20');",
  "if (typeof syncTileColorMode === 'function') syncTileColorMode(tab);"]) assert.ok(editor.includes(marker), marker);
// Loading migrates: "From icon", "From cover" (tile) and "Tint tile" are Fill.
assert.match(editor, /if \(fill\) fill\.checked = parsed\.fill > 0 \|\| \(media && parsed\.cover\.tile > 0\) \|\| Number\(parsed\.source\?\.tile \|\| 0\) > 0;/);
for (const [lang, text] of [['en', 'While a rule matches, it wins. Otherwise the settings above apply.'],
  ['de', 'Solange eine Regel zutrifft, gewinnt sie. Sonst gelten die Einstellungen oben.'],
  ['fr', "Tant qu'une règle s'applique, elle l'emporte. Sinon, les réglages ci-dessus s'appliquent."]]) {
  assert.ok(read('src/core/i18n/i18n.cpp').includes(`"${text}"`), `${lang} rule priority hint`);
}
for (const text of ['"Ausfüllen"', '"Fill"', '"Remplir"', '"Wypełnij"']) assert.ok(read('src/core/i18n/i18n.cpp').includes(text), text);

// Native parity: normalize(), fill_of(), fills(), the shown icon,
// choose() + without_white and background() give the preview's results.
const compiler = [process.env.CXX, 'clang++', 'g++'].filter(Boolean)
  .find(candidate => spawnSync(candidate, ['--version']).status === 0);
let native = false;
if (!compiler) {
  console.log('SKIP: tile color native parity needs a C++ compiler');
} else {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'hometiles-tile-color-'));
  const hex = value => `0x${String(value).slice(1)}u`;
  try {
    const cpp = path.join(dir, 'main.cpp');
    fs.writeFileSync(cpp, `#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstddef>
#include "src/tiles/config/tile_icon_colors.h"
#include "src/tiles/config/tile_tint.h"
#include "src/ui/shared/tone_color.h"
// tile_icon_source.cpp refresh_card: a rule that colors the icon wins over
// the own state color; the fill follows the icon (without_white).
static void row(const char* record, const char* state, const char* display, uint32_t fallback) {
  uint32_t icon = tile_icon_colors::state_icon_color(record, true, state, display, fallback);
  const tile_icon_colors::Source layer = tile_icon_colors::source_of(record);
  uint32_t rule = 0;
  const bool colored = layer.mode != tile_icon_colors::SourceMode::None && layer.enabled &&
                       tile_icon_colors::resolve(record, state, display, rule, false);
  if (colored && tile_icon_colors::rule_colors_icon(layer)) icon = rule;
  const uint8_t fill = tile_icon_colors::fills(record) ? tile_icon_colors::kTintDefault : 0;
  tile_tint::Choice choice = tile_tint::choose(false, 0, 0, fill, icon);
  if (choice.percent && tone_color::no_color(choice.color)) choice = tile_tint::Choice{};
  std::printf("#%06X|", static_cast<unsigned>(icon));
  if (choice.percent) {
    std::printf("#%06X@%u|#%06X\\x1e", static_cast<unsigned>(choice.color), static_cast<unsigned>(choice.percent),
                static_cast<unsigned>(tile_tint::background(${hex(GLOBAL)}, choice.color, choice.percent)));
  } else {
    std::printf("-|-\\x1e");
  }
}
int main() {
  static char out[tile_icon_colors::kMaxRecordBytes + 1];
${cases.map(([input]) => `  tile_icon_colors::normalize(${JSON.stringify(input)}, out, sizeof(out), false, false, true, false);
  std::printf("%s\\x1f%u\\x1e", out, static_cast<unsigned>(tile_icon_colors::fill_of(${JSON.stringify(input)})));`).join('\n')}
${fillRecords.map(([input]) => `  std::printf("%u\\x1e", tile_icon_colors::fills(${JSON.stringify(input)}) ? static_cast<unsigned>(tile_icon_colors::kTintDefault) : 0u);`).join('\n')}
${scenarios.map(([, , record, state, display, fallback]) =>
    `  row(${JSON.stringify(record)}, ${JSON.stringify(state)}, ${display === null ? 'nullptr' : JSON.stringify(display)}, ${hex(fallback)});`).join('\n')}
  return 0;
}
`);
    const exe = path.join(dir, process.platform === 'win32' ? 'main.exe' : 'main');
    let result = spawnSync(compiler, ['-std=c++17', '-Wall', '-Werror', '-D_CRT_SECURE_NO_WARNINGS', '-I', repoRoot, cpp, '-o', exe], {encoding: 'utf8'});
    assert.equal(result.status, 0, result.stderr || result.stdout);
    result = spawnSync(exe, [], {encoding: 'utf8'});
    assert.equal(result.status, 0, result.stderr);
    const rows = result.stdout.replace(/\r\n/g, '\n').split('\x1e');
    cases.forEach(([input, expected, fill], i) => {
      const [normalized, nativeFill] = rows[i].split('\x1f');
      assert.equal(normalized, expected, `native normalize ${JSON.stringify(input)}`);
      assert.equal(Number(nativeFill), fill, `native fill ${JSON.stringify(input)}`);
    });
    fillRecords.forEach(([input, fill], i) => {
      assert.equal(Number(rows[cases.length + i]), fill, `native fills ${JSON.stringify(input)}`);
    });
    scenarios.forEach(([name], i) => {
      assert.equal(rows[cases.length + fillRecords.length + i], preview[i], `${name}: firmware == preview`);
    });
    native = true;
  } finally {
    fs.rmSync(dir, {recursive: true, force: true});
  }
}
// New tiles start in the HomeTiles look: choosing a type with icon colors on
// an empty tile turns on "Fill"; existing tiles and the screensaver keep
// their style.
{
  const editorJs = read('src/web/admin/tiles/editor.js');
  const start = editorJs.indexOf('New tiles start in the HomeTiles look');
  assert.ok(start > 0, 'New tile style block exists');
  const block = editorJs.slice(start, editorJs.indexOf('normalizeLayoutInputs(tab);', start));
  assert.match(block, /previousType === 0 && nextType !== 0 && !isScreensaverTileTab\(tab\)/);
  assert.match(block, /tileTypeHasIconColors\(String\(nextType\)\)/);
  assert.match(block, /_tile_icon_fill'\);\s*if \(fill\) fill\.checked = true;/);
  assert.doesNotMatch(block, /strength/);
  assert.match(block, /syncTileColorMode\(tab\);/);
  assert.ok(editorJs.indexOf('updateTileType(tab);', start - 400) < start,
    'The style is set after the type fields are switched');
}
console.log(`Tile color choice: the fill follows the icon, rules color the icon, Desk and Water pass${native ? '; firmware == preview' : ''}`);
