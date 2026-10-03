// b205 read white balance statistics only near the AE target, to save a frame
// per step while the exposure still moves. In a dark room the exposure stays
// at its limit and never gets near: white balance never ran, and the V2 image
// kept the green of unbalanced gains (awb_samples 0, 2026-10-03). Once the
// step is final (in the band or at the limit) white balance runs anyway.
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';
import {cppFunctionDefinitions, maskCpp} from '../../lib/cpp-source.mjs';

const source = maskCpp(readRepoFile('src/video/local_camera/local_camera.cpp')).replace(/\r\n?/g, '\n');
const body = name => {
  const found = cppFunctionDefinitions(source).find(f => f.name === name);
  assert.ok(found, name);
  return found.source;
};

for (const [name, luma] of [['captureJpeg', 'stats->mean_luma'], ['streamSettle', 'run.mean_luma']]) {
  const loop = body(name);
  const step = loop.indexOf('const ExposureStep step');
  const awb = loop.indexOf('esp_isp_awb_controller_get_oneshot_statistics(');
  assert.ok(step > 0 && awb > step, `${name}: the step is known before white balance`);
  const condition = loop.slice(loop.lastIndexOf('if (', awb), awb);
  assert.ok(condition.includes(`(step.converged || nearAeTarget(${luma}))`),
    `${name}: white balance also runs at the exposure limit`);
}
console.log('Local camera: white balance runs once the exposure is final, also at its limit');
