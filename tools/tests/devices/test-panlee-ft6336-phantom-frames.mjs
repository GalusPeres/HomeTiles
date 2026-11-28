// Panlee ZX3D95CE01S ghost triggers: the FT6336 sometimes returns a leftover
// release frame (TD_STATUS 0x41, event flag "put up", touch id 4, fixed point
// 321,321) that the driver read as a touch and clicked the tile under it.
// This runs the production frame check on the frames seen on the hardware.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

import {compileAndRun} from '../../lib/cpp-host.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const driver = fs.readFileSync(
  path.join(root, 'src/devices/panlee_zx3d95ce01s/device_panlee_zx3d95ce01s.cpp'), 'utf8');
assert.match(driver, /ft6336::isLiveTouchFrame\(frame\)/,
  'getTouch must filter every frame through the shared FT6336 check');

const frame = (status, x, y, event = 0, id = 0) =>
  [status, (event << 6) | (x >> 8), x & 0xFF, (id << 4) | (y >> 8), y & 0xFF];
const cases = [
  // [label, frame, live]
  ['real down', frame(0x01, 112, 383), true],
  ['real down 2', frame(0x01, 368, 186), true],
  ['contact', frame(0x01, 200, 200, 2), true],
  ['second finger id 1', frame(0x02, 200, 200, 0, 1), true],
  ['ghost seen on hardware', frame(0x41, 321, 321, 1, 4), false],
  ['put up with one point', frame(0x01, 321, 321, 1, 0), false],
  ['invalid touch id', frame(0x01, 100, 100, 0, 4), false],
  ['undefined status bits', frame(0x41, 100, 100, 0, 0), false],
  ['no points', frame(0x00, 0, 0), false],
];

const lines = cases.map(([, f]) =>
  `  {${f.map(byte => `0x${byte.toString(16)}`).join(',')}},`).join('\n');
const output = compileAndRun({
  label: 'FT6336 frame check',
  harness: `#include <stdio.h>
#include "src/devices/panlee_zx3d95ce01s/ft6336_frame.h"
int main() {
  const uint8_t frames[][5] = {
${lines}
  };
  for (const auto& f : frames) printf("%d\\n", ft6336::isLiveTouchFrame(f) ? 1 : 0);
  return 0;
}
`});
if (output !== null) {
  const results = output.trim().split('\n').map(Number);
  cases.forEach(([label, , live], index) => {
    assert.equal(results[index], live ? 1 : 0, label);
  });
}
console.log('Panlee FT6336 phantom frames: PASS');
