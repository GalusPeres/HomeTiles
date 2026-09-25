// "Tint tile" of the icon color: an option under the fixed icon color that
// tints the tile with that color at a strength (record line "fill NN", like
// the rules' "tile=NN"). It needs a fixed color, is clamped like the rule
// tint, applies below an active rule tint and leaves existing records alone.
// Firmware (native normalize/fill_of) and the Web Admin mirror agree.
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

// [input, expected normalized record, expected fill]
const cases = [
  ['v2\nFF0000\nfill 25', 'v2\nFF0000\nfill 25', 25],
  ['v2\nFF0000\nfill 99', 'v2\nFF0000\nfill 50', 50],
  ['v2\nFF0000\nfill 3', 'v2\nFF0000\nfill 10', 10],
  ['v2\n\nfill 25', '', 0],
  ['v2\nFF0000\nfill x', 'v2\nFF0000', 0],
  ['v2\nFF0000', 'v2\nFF0000', 0],
  ['v2\nFF0000\nsrc auto light.kitchen tile=30\nfill 20', 'v2\nFF0000\nfill 20\nsrc auto light.kitchen tile=30', 20],
];
for (const [input, expected, fill] of cases) {
  const normalized = js.normalizeIconColorRecord(input, false, false, true, false);
  assert.equal(normalized, expected, `JS normalize ${JSON.stringify(input)}`);
  assert.equal(js.parseIconColorRecord(input).fill, fill, `JS fill ${JSON.stringify(input)}`);
}

// Preview: the fixed color tints the tile; an active rule tint wins.
const meta = {values: {'light.on': JSON.stringify({state: 'on', rgb_color: [0, 0, 255]}), 'light.off': 'off'}};
assert.deepEqual({...js.iconColorTilePreviewTint('1', 'v2\nFF0000\nfill 25', 'sensor.x', meta)}, {color: '#FF0000', percent: 25});
assert.equal(js.iconColorTilePreviewTint('1', 'v2\nFF0000', 'sensor.x', meta), null, 'No option, no tint');
assert.deepEqual({...js.iconColorTilePreviewTint('2', 'v2\nFF0000\nfill 25\nsrc auto light.on tile=30', '', meta)},
  {color: '#0000FF', percent: 30}, 'An active rule tint wins');
assert.deepEqual({...js.iconColorTilePreviewTint('2', 'v2\nFF0000\nfill 25\nsrc auto light.off tile=30', '', meta)},
  {color: '#FF0000', percent: 25}, 'An inactive rule falls back to the icon color tint');

// Firmware: refresh_card applies the fill below the rule tint.
const source = read('src/tiles/runtime/tile_icon_source.cpp');
assert.match(source, /if \(colored && active && layer\.tile\) \{\s*set_tile_tint\(card, rgb, layer\.tile\);\s*\} else if \(fill && tile_icon_colors::fixed_color\(tile\.icon_colors\.c_str\(\), fixed\)\) \{\s*set_tile_tint\(card, fixed, fill\);/);
// Editor: the option sits under the icon color, with the rules' labels.
const html = read('src/web/server/render/tile_icon_colors_html.cpp');
for (const marker of ['_tile_icon_fill" data-icon-color="fill-target"> )html";', 'appendHtmlEscaped(html, tr.tile_rules_tint_tile);',
  'data-icon-color="fill-strength"', 'data-icon-color="fill-strength-reset"']) assert.ok(html.includes(marker), marker);
const editor = read('src/web/admin/tiles/icon-colors.js');
for (const marker of ["if (fixed && iconColorEl(tab, '_tile_icon_fill')?.checked) {",
  "lines.push('fill ' + (iconColorEl(tab, '_tile_icon_fill_strength')?.value || '20'));",
  "if (fill) fill.checked = parsed.fill > 0;",
  "iconColorEl(tab, '_tile_icon_fill_row')?.classList.toggle('hidden', !hasFixed);"]) assert.ok(editor.includes(marker), marker);

// A tile color picked by hand is kept: picking it switches off "Tint tile" of
// the rules and of the icon color (both would replace it).
assert.match(read('src/web/admin/tiles/grid-preview.js'),
  /function markTileColorInputExplicit\(tab\) \{[\s\S]*?for \(const suffix of \['_tile_icon_rule_tile', '_tile_icon_fill'\]\) \{[\s\S]*?tint\.checked = false;[\s\S]*?syncIconColorFields\(tab\);/);
{
  const src = read('src/web/admin/tiles/grid-preview.js');
  const start = src.indexOf('function markTileColorInputExplicit(');
  let depth = 0, end = src.indexOf('{', start);
  for (; end < src.length; end++) { if (src[end] === '{') depth++; else if (src[end] === '}' && --depth === 0) break; }
  const elements = {t_tile_color: {dataset: {}}, t_tile_icon_rule_tile: {checked: true}, t_tile_icon_fill: {checked: true}};
  let synced = 0;
  const mark = new Function('document', 'syncTileColorGlobalToggle', 'syncIconColorFields',
    src.slice(start, end + 1) + '; return markTileColorInputExplicit;')(
    {getElementById: id => elements[id] || null}, () => {}, () => { synced++; });
  mark('t');
  assert.ok(!elements.t_tile_icon_rule_tile.checked && !elements.t_tile_icon_fill.checked && synced === 1,
    'Picking a tile color switches both tints off');
  assert.equal(elements.t_tile_color.dataset.bgColorDefault, '0');
}

// Native parity of normalize() and fill_of().
const compiler = [process.env.CXX, 'clang++', 'g++'].filter(Boolean)
  .find(candidate => spawnSync(candidate, ['--version']).status === 0);
let native = false;
if (!compiler) {
  console.log('SKIP: fill native parity needs a C++ compiler');
} else {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'hometiles-fill-'));
  try {
    const cpp = path.join(dir, 'main.cpp');
    fs.writeFileSync(cpp, `#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstddef>
#include "src/tiles/config/tile_icon_colors.h"
int main() {
  static char out[tile_icon_colors::kMaxRecordBytes + 1];
${cases.map(([input]) => `  tile_icon_colors::normalize(${JSON.stringify(input)}, out, sizeof(out), false, false, true, false);
  std::printf("%s\\x1f%u\\x1e", out, static_cast<unsigned>(tile_icon_colors::fill_of(${JSON.stringify(input)})));`).join('\n')}
  return 0;
}
`);
    const exe = path.join(dir, process.platform === 'win32' ? 'main.exe' : 'main');
    let result = spawnSync(compiler, ['-std=c++17', '-Wall', '-Werror', '-D_CRT_SECURE_NO_WARNINGS', '-I', repoRoot, cpp, '-o', exe], {encoding: 'utf8'});
    assert.equal(result.status, 0, result.stderr || result.stdout);
    result = spawnSync(exe, [], {encoding: 'utf8'});
    assert.equal(result.status, 0, result.stderr);
    const rows = result.stdout.replace(/\r\n/g, '\n').split('\x1e').slice(0, cases.length).map(row => row.split('\x1f'));
    cases.forEach(([input, expected, fill], i) => {
      assert.equal(rows[i][0], expected, `native normalize ${JSON.stringify(input)}`);
      assert.equal(Number(rows[i][1]), fill, `native fill ${JSON.stringify(input)}`);
    });
    native = true;
  } finally {
    fs.rmSync(dir, {recursive: true, force: true});
  }
}
console.log(`Icon color "Tint tile" option passes${native ? '; firmware == preview' : ''}`);
