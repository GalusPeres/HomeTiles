// Tile color is one choice (Global | Custom | From icon). "From icon" is the
// record line "fill NN" (clamped like the rule tint "tile=NN"): the tile
// always follows the color the icon shows (own, Home Assistant or a rule's
// "Color icon" color), so a rule's "Tint tile" does not apply and the editor
// hides it. Otherwise a rule "Tint tile" tints while it applies. One tint
// rule for device and preview (tile_tint::choose == tileTintChoice); grey,
// white and black never tint. One icon color rule
// (tile_icon_colors::state_icon_color == previewIconColor): own rules color
// the icon only with "Color icon". The maintainer's Desk (Binary sensor) and
// Water (Sensor bar) tiles run through both, natively and in the preview.
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

// The real preview icon color branch (grid-preview.js previewIconColor) with
// the tile's own state handed in like the preview does.
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
vm.runInContext(functionSource(grid, 'previewIconColor'), js);

// [input, expected normalized record, expected fill]
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

// Scenarios: [name, type, record, state, display, type color, entity icon].
// The entity icon stands in for an entity color (a light) when the record
// has no own icon color.
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
  ['Custom color, rule applies', '1', 'v2\n\nsrc rules self tile=30\nhas F44336 6', 'in 6 Tagen', null, '#FFFFFF'],
  ['Custom color, no rule', '1', 'v2\n\nsrc rules self tile=30\nhas F44336 6', 'in 5 Tagen', null, '#FFFFFF'],
  ['From icon color, own icon color', '1', 'v2\n00BCD4\nfill 35', '12', null, '#FFFFFF'],
  ['From icon color, white icon', '1', 'v2\n\nfill 35', '12', null, '#FFFFFF'],
  ['From icon ignores Tint tile', '1', 'v2\nD63B3B\nfill 20\nsrc rules self tile=30 noicon\nbar smooth 20 70 0:3B82F6 540:EF4444', '47.7', null, '#FFFFFF'],
].map(([name, type, record, state, display, fallback]) =>
  [name, type, js.normalizeIconColorRecord(record, true, true, true, true), state, display, fallback]);
for (const scenario of scenarios) assert.ok(scenario[2].startsWith('v2\n'), `normalized ${scenario[0]}: ${scenario[2]}`);

const GLOBAL = '#181818';
const preview = scenarios.map(([, type, record, state, display, fallback]) => {
  const entity = 'sensor.x';
  const meta = {values: {[entity]: state}};
  const icon = js.normalizeIconColorHex(js.previewIconColor(type, record, entity, meta,
    type === '20' ? {state, display} : null, fallback)) || fallback;
  const layer = js.iconColorRecordSource(record);
  const rule = layer && layer.enabled ? js.resolveIconColorRecord(record, state, display, false) : '';
  const choice = js.tileTintChoice(!!rule, rule, layer?.tile || 0, js.parseIconColorRecord(record).fill, icon);
  const bg = choice ? js.tileTintBackground(GLOBAL, choice.color, choice.percent) : '-';
  return [icon, rule || '-', choice ? `${choice.color}@${choice.percent}` : '-', bg].join('|');
});
const expect = (name, predicate) => {
  const index = scenarios.findIndex(s => s[0] === name);
  assert.ok(predicate(preview[index].split('|')), `${name}: ${preview[index]}`);
};
// Desk uses From icon: the tile follows the icon (yellow, or the rule color
// with "Color icon"); its grey Clear color never tints.
expect('Desk detected', ([icon, , choice]) => icon === '#FFC107' && choice === '#FFC107@20');
expect('Desk clear', ([icon, rule, choice]) => icon === '#FFC107' && rule === '#A0A0A0' && choice === '#FFC107@20');
expect('Desk with Color icon, clear', ([icon, , choice]) => icon === '#A0A0A0' && choice === '-');
expect('Desk with Color icon, detected', ([icon, , choice]) => icon === '#787161' && choice === '#787161@20');
// Water uses Global and "Tint tile": the tile takes the bar color, the icon
// stays red.
expect('Water 47.7', ([icon, rule, choice]) => icon === '#D63B3B' && rule === '#EF4444' && choice === '#EF4444@20');
expect('Water 25', ([icon, , choice]) => icon === '#D63B3B' && choice.endsWith('@20') && choice !== '-');
expect('Custom color, rule applies', ([, , choice]) => choice === '#F44336@30');
expect('Custom color, no rule', ([, , choice]) => choice === '-');
expect('From icon color, own icon color', ([icon, , choice]) => icon === '#00BCD4' && choice === '#00BCD4@35');
expect('From icon color, white icon', ([, , choice]) => choice === '-');
expect('From icon ignores Tint tile', ([icon, rule, choice]) => icon === '#D63B3B' && rule === '#EF4444' && choice === '#D63B3B@20');

// Preview: the rule tint and the From icon color tint go through tileTintChoice.
for (const marker of [
  "el.dataset.ruleTint = tint ? '1' : '0';",
  'if (fill) el.dataset.iconFill = String(fill);',
  "tileTintChoice(false, '', 0, fill, '#' + iconRgb.map(v => v.toString(16).padStart(2, '0')).join(''))",
  'tileElem.style.background = tileTintBackground(base || \'#222222\', choice.color, choice.percent);',
  'tileElem.style.background = tileElem.dataset.baseBg;',
  "if (input && input.dataset.unset === '1' && shown) {",
]) assert.ok(grid.includes(marker), 'grid-preview: ' + marker);
assert.ok(read('src/web/admin/tiles/icon-colors.js').includes("return tileTintChoice(true, color, layer.tile, 0, '');") &&
  read('src/web/admin/tiles/icon-colors.js').includes('if (parseIconColorRecord(record).fill) return null;'),
  'The preview applies no rule tint while the tile follows the icon');

// Firmware: refresh_card and the icon color hook both decide through
// tile_tint::choose; icon color changes reach the hook through apply_fill().
const source = read('src/tiles/runtime/tile_icon_source.cpp');
for (const marker of [
  'tile_tint::choose(colored && active, rgb, layer.tile, fill, disc_icon_rgb(find_disc(card)));',
  'set_icon_fill_marker(card, fill);',
  'apply_tint_choice(card, tile_tint::choose(false, 0, 0, marker, disc_icon_rgb(disc)));',
  'if (!card || !marker) return;',
  'tile_icon_disc::g_icon_color_hook = &on_icon_color;',
]) assert.ok(source.includes(marker), 'tile_icon_source: ' + marker);
assert.ok(read('src/tiles/runtime/tile_icon_disc.h').includes('if (g_icon_color_hook) g_icon_color_hook(disc);'));

// Editor: the strength sits with the tile color, the choice is a segmented
// control, the rules say that they win, and nothing unchecks a rule.
const html = read('src/web/server/render/tile_icon_colors_html.cpp');
for (const marker of ['void append_tile_color_from_icon_html(String& html, const String& tab_id) {',
  '_tile_icon_fill" hidden>', 'data-icon-color="fill-strength"', 'data-icon-color="fill-strength-reset"',
  'appendHtmlEscaped(html, tr.tile_rules_priority_hint);', '_tile_icon_rule_follows_icon">)html";',
  'appendHtmlEscaped(html, tr.tile_rules_tile_follows_icon);']) assert.ok(html.includes(marker), marker);
const fixed = html.slice(html.indexOf('void append_tile_icon_color_fixed_html('), html.indexOf('void append_tile_color_from_icon_html('));
assert.doesNotMatch(fixed, /_tile_icon_fill|tile_rules_tint_tile/, 'No second "Tint tile" under the icon color');
const editor = read('src/web/admin/tiles/icon-colors.js');
for (const marker of ["if (iconColorEl(tab, '_tile_icon_fill')?.checked) {",
  "lines.push('fill ' + (iconColorEl(tab, '_tile_icon_fill_strength')?.value || '20'));",
  'if (fill) fill.checked = parsed.fill > 0;',
  "if (typeof syncTileColorMode === 'function') syncTileColorMode(tab);"]) assert.ok(editor.includes(marker), marker);
const mark = functionSource(grid, 'markTileColorInputExplicit');
assert.doesNotMatch(mark, /_tile_icon_rule_tile/, 'Picking a tile color never switches a rule off');
for (const [lang, text] of [['en', 'While a rule matches, it wins. Otherwise the settings above apply.'],
  ['de', 'Solange eine Regel zutrifft, gewinnt sie. Sonst gelten die Einstellungen oben.'],
  ['fr', "Tant qu'une règle s'applique, elle l'emporte. Sinon, les réglages ci-dessus s'appliquent."]]) {
  assert.ok(read('src/core/i18n/i18n.cpp').includes(`"${text}"`), `${lang} rule priority hint`);
}

// Native parity: normalize(), fill_of(), state_icon_color(), choose() and
// background() give the preview's results for every scenario.
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
static void row(const char* record, const char* state, const char* display, uint32_t fallback) {
  const uint32_t icon = tile_icon_colors::state_icon_color(record, true, state, display, fallback);
  const tile_icon_colors::Source layer = tile_icon_colors::source_of(record);
  uint32_t rule = 0;
  const bool colored = layer.mode != tile_icon_colors::SourceMode::None && layer.enabled &&
                       tile_icon_colors::resolve(record, state, display, rule, false);
  const tile_tint::Choice choice =
      tile_tint::choose(colored, rule, layer.tile, tile_icon_colors::fill_of(record), icon);
  std::printf("#%06X|", static_cast<unsigned>(icon));
  if (colored) std::printf("#%06X|", static_cast<unsigned>(rule)); else std::printf("-|");
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
    scenarios.forEach(([name], i) => {
      assert.equal(rows[cases.length + i], preview[i], `${name}: firmware == preview`);
    });
    native = true;
  } finally {
    fs.rmSync(dir, {recursive: true, force: true});
  }
}
console.log(`Tile color choice: one tint rule, one icon color rule, Desk and Water pass${native ? '; firmware == preview' : ''}`);
