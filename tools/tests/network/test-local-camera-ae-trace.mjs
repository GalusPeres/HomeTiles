// V2 2026-10-03: with brightness +50 a stream ended at the minimum gain in a
// dark room, and the status showed only the final exposure. The last AE
// evaluations outside the target band (still image, stream start and running
// stream) are kept in a small ring and listed in /api/local-camera, so the
// step that pulled the exposure down shows without a USB log.
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';
import {cppFunctionDefinitions, maskCpp} from '../../lib/cpp-source.mjs';

const raw = readRepoFile('src/video/local_camera/local_camera.cpp').replace(/\r\n?/g, '\n');
const source = maskCpp(raw);
const body = name => {
  const found = cppFunctionDefinitions(source).find(f => f.name === name);
  assert.ok(found, name);
  return found.source;
};

assert.match(source, /constexpr size_t kAeTraceSize = 24;/);
const trace = body('traceAutoExposure');
assert.match(trace, /g_ae_trace\[g_ae_trace_count % kAeTraceSize\] = entry;/);
assert.match(trace, /portENTER_CRITICAL\(&g_ae_trace_mux\);/, 'worker and web handler share it');

// Character literals stay unmasked here.
const rawBody = name => cppFunctionDefinitions(raw).find(f => f.name === name).source;
assert.match(rawBody('captureJpeg'), /traceAutoExposure\('s', iteration,/);
assert.match(rawBody('streamSettle'), /traceAutoExposure\('b', iteration,/);
assert.match(rawBody('streamAutoTune'), /if \(!step\.converged\) traceAutoExposure\('t', 0, 0, run\.mean_luma\);/,
  'the running stream records every step outside the band');

const status = cppFunctionDefinitions(raw).find(f => f.name === 'appendStatusJson');
assert.ok(status, 'appendStatusJson');
assert.ok(status.source.includes('json += ",\\"ae_trace\\":[";'));
assert.ok(status.source.includes('"%s[%u,\\"%c\\",%u,%u,%u,%u,%u]"'));
console.log('Local camera: the last AE steps outside the band are listed in /api/local-camera');
