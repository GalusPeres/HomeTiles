import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

// Issue #43: climate fan and swing lists must keep Home Assistant's own option
// names (for example "1", "2", "silent") instead of a fixed set of known names.
const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = (file) => fs.readFileSync(path.join(repoRoot, file), 'utf8').replace(/\r\n/g, '\n');

const state = read('src/types/climate/state.h');
const renderer = read('src/tiles/runtime/tile_renderer.cpp');
const climateRenderer = read('src/types/climate/renderer.cpp');

for (const field of ['fan_modes', 'swing_modes', 'swing_horizontal_modes']) {
  assert.match(state, new RegExp(`char ${field}\\[\\d+\\] = \\{\\};`), `ClimateState keeps ${field} as text`);
  assert.doesNotMatch(state, new RegExp(`${field}_mask`), `${field} must not be reduced to a mask`);
  assert.match(
    renderer,
    new RegExp(`climate_copy_mode_list\\(\\s*out\\.${field},\\s*sizeof\\(out\\.${field}\\), modes\\)`),
    `parse_climate_payload copies ${field} unchanged`,
  );
  for (const [name, source] of [['tile_renderer.cpp', renderer], ['climate/renderer.cpp', climateRenderer]]) {
    assert.match(source, new RegExp(`init\\.${field} = state\\.${field};`), `${name} passes ${field} to the popup`);
  }
}

// The copy keeps inner spaces (for example "swing up-down") and only removes
// the JSON array syntax.
const copy = renderer.slice(
  renderer.indexOf('static void climate_copy_mode_list('),
  renderer.indexOf('static ClimateState parse_climate_payload('),
);
assert.ok(copy.length > 0, 'climate_copy_mode_list is missing');
assert.doesNotMatch(copy, /replace\(" ", ""\)/, 'option names keep their inner spaces');
assert.match(copy, /needed < out_size/, 'the list stays within its bounded buffer');

console.log('PASS: climate fan and swing lists keep vendor option names');
