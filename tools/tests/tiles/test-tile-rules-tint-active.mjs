// "Entity color" tints a tile only while the entity is active: a light or
// switch that is on, a Binary sensor that is on, a Climate entity that runs
// (not off) and a Cover that is not closed. Off, closed and unknown states
// keep the tile's own color; the icon still shows the entity's grey. Own
// rules tint while one matches. Checks the firmware path (native Climate
// activity, source contracts) and the Web Admin preview mirror.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import vm from 'node:vm';
import {spawnSync} from 'node:child_process';
import {readRepoFile, repoRoot} from '../../lib/admin-source.mjs';

const read = file => readRepoFile(file).replace(/\r\n?/g, '\n');

// ---------------------------------------------------------------------------
// Firmware contracts: every auto color reports activity and refresh_card()
// tints only active entities.
const source = read('src/tiles/runtime/tile_icon_source.cpp');
for (const marker of [
  'bool auto_color(const String& domain, const char* payload, uint32_t& rgb, bool& active) {',
  'if (switch_domain(domain)) return switch_payload_icon_color(payload, rgb, &active);',
  'if (domain == "climate") return climate_payload_icon_color(payload, rgb, &active);',
  'if (domain == "cover") return cover_payload_icon_color(payload, rgb, &active);',
  'active = state.value == BinarySensorValue::On;',
  'rule_color(tile, rgb, &active);',
  'const bool rule_tint = colored && active && layer.tile;',
]) assert.ok(source.includes(marker), `tile_icon_source: ${marker}`);
const renderer = read('src/tiles/runtime/tile_renderer.cpp');
assert.ok(renderer.includes('if (active) *active = state.is_on;'), 'Switch activity is the on state');
assert.ok(renderer.includes('if (active) *active = climate_visuals::state_active(state.hvac_mode, state.hvac_action);'),
  'Climate activity comes from climate_visuals');
assert.match(read('src/types/cover/renderer.cpp'),
  /if \(active\) \*active = strcmp\(state\.state, "unknown"\) != 0 && strcmp\(state\.state, "unavailable"\) != 0 &&\s*strcmp\(state\.state, "closed"\) != 0;/,
  'Cover activity matches cover_icon_color()');

// ---------------------------------------------------------------------------
// Web Admin preview mirror.
const context = vm.createContext({
  document: {addEventListener() {}, getElementById: () => null, querySelector: () => null,
    querySelectorAll: () => [], documentElement: {lang: 'en'}},
  window: {}, navigator: {language: 'en'}, TextEncoder, TextDecoder, Number, Math, String, Array, JSON,
  // Icon names are not under test (tiles/state.js).
  normalizeMdiIconName: value => String(value || ''),
});
for (const file of ['src/web/admin/core/localization.js', 'src/types/switch/admin.js',
  'src/types/binary_sensor/admin-state.js', 'src/types/climate/admin-preview.js', 'src/types/cover/admin.js',
  'src/web/admin/tiles/icon-colors.js']) {
  vm.runInContext(read(file), context, {filename: file});
}
const climate = (mode, action) => JSON.stringify({state: mode, attributes: action ? {hvac_action: action} : {}});
const meta = {values: {
  'light.on': JSON.stringify({state: 'on', rgb_color: [255, 0, 0]}),
  'light.off': 'off',
  'switch.unavailable': 'unavailable',
  'binary_sensor.open': 'on',
  'binary_sensor.closed': 'off',
  'climate.cool': climate('cool', 'cooling'),
  'climate.heat_idle': climate('heat', 'idle'),
  'climate.off': climate('off', ''),
  'climate.action_off': climate('heat', 'off'),
  'climate.unknown': 'unknown',
  'cover.open': 'open',
  'cover.closing': 'closing',
  'cover.closed': 'closed',
  'sensor.waste': 'in 6 Tagen rausstellen',
}};
const tint = (entity, record = `v2\n\nsrc auto ${entity} tile=20`) =>
  context.iconColorTilePreviewTint('4', record, '', meta);
const expectations = [
  ['light.on', true], ['light.off', false], ['switch.unavailable', false],
  ['binary_sensor.open', true], ['binary_sensor.closed', false],
  ['climate.cool', true], ['climate.heat_idle', true], ['climate.off', false],
  ['climate.action_off', false], ['climate.unknown', false],
  ['cover.open', true], ['cover.closing', true], ['cover.closed', false],
];
for (const [entity, active] of expectations) {
  assert.equal(!!tint(entity), active, `${entity} ${active ? 'tints' : 'keeps the tile color'}`);
  assert.equal(context.iconColorSourceAutoActive(entity, meta.values[entity]), active, `${entity} activity`);
}
// The icon still takes the grey of an inactive entity.
assert.equal(context.iconColorSourcePreview('v2\n\nsrc auto light.off tile=20', meta), '#B0B0B0');
// The own light color of a Switch tile tints while it is on.
const own = context.iconColorTilePreviewTint('5', 'v2\n\nsrc auto self tile=25', 'light.on', meta);
assert.ok(own && own.color === '#FF0000' && own.percent === 25, 'Switch tints with its own light color');
assert.equal(context.iconColorTilePreviewTint('5', 'v2\n\nsrc auto self tile=25', 'light.off', meta), null,
  'A switched-off Switch keeps its tile color');
// Own rules tint while one matches, whatever the state means.
assert.ok(tint('sensor.waste', 'v2\n\nsrc rules sensor.waste tile=20\nhas F44336 6'), 'Matching own rule tints');
assert.equal(tint('sensor.waste', 'v2\n\nsrc rules sensor.waste tile=20\nhas F44336 9'), null, 'No match, no tint');

// ---------------------------------------------------------------------------
// Native: climate_visuals::state_active() == preview for the same states.
const compiler = [process.env.CXX, 'clang++', 'g++'].filter(Boolean)
  .find(candidate => spawnSync(candidate, ['--version']).status === 0);
let nativeChecked = false;
if (!compiler) {
  console.log('SKIP: Climate activity native check needs a C++ compiler');
} else {
  const pairs = [['cool', 'cooling'], ['heat', 'idle'], ['heat', 'heating'], ['off', ''], ['heat', 'off'],
    ['unknown', ''], ['', ''], ['off', 'heating'], ['auto', ''], ['fan_only', 'fan'], ['dry', 'drying']];
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'hometiles-climate-active-'));
  try {
    fs.writeFileSync(path.join(dir, 'Arduino.h'),
      '#pragma once\n#include <cstdint>\n#include <string>\n' +
      'class String : public std::string { public: using std::string::string; };\n');
    const cppPath = path.join(dir, 'main.cpp');
    fs.writeFileSync(cppPath, `#include "src/types/climate/visuals.h"
#include <cstdio>
int main() {
${pairs.map(([mode, action]) => `  std::printf("%d\\n", climate_visuals::state_active(${JSON.stringify(mode)}, ${JSON.stringify(action)}) ? 1 : 0);`).join('\n')}
  return 0;
}
`);
    const executable = path.join(dir, process.platform === 'win32' ? 'main.exe' : 'main');
    let result = spawnSync(compiler, ['-std=c++17', '-Wall', '-Werror', '-I', dir, '-I', repoRoot, cppPath, '-o', executable],
      {encoding: 'utf8'});
    assert.equal(result.status, 0, result.stderr || result.stdout);
    result = spawnSync(executable, [], {encoding: 'utf8'});
    assert.equal(result.status, 0, result.stderr);
    const native = result.stdout.trim().split(/\r?\n/).map(line => line === '1');
    pairs.forEach(([mode, action], index) => {
      const payload = mode ? climate(mode, action) : JSON.stringify({attributes: {}});
      assert.equal(context.iconColorSourceAutoActive('climate.x', payload), native[index],
        `Climate ${mode || '(empty)'}/${action || '-'}: firmware ${native[index]}`);
    });
    nativeChecked = true;
  } finally {
    fs.rmSync(dir, {recursive: true, force: true});
  }
}
console.log(`Entity color tints only active entities${nativeChecked ? '; firmware Climate activity == preview' : ''}`);
