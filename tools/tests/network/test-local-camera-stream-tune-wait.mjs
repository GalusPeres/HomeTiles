// V2 2026-10-03: in a running stream the gain went down with the Max. gain
// setting but never came back up. The AE statistics arrive with the end of
// the next sensor frame, and the read waited at most the time left before the
// next deadline (capped at 40 ms). At 25 fps the frame (1331 lines, 39 ms) is
// about as long as the period, at 15 fps (2328 lines, 68 ms) longer than any
// allowed wait: every read timed out, and the forced read only applied when
// almost no time was left. Now a read waits a whole frame once nothing was
// measured for kStreamTuneForceMs, and a failed read is retried as the same
// kind instead of alternating.
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';
import {cppFunctionDefinitions, maskCpp} from '../../lib/cpp-source.mjs';

const source = maskCpp(readRepoFile('src/video/local_camera/local_camera.cpp')).replace(/\r\n?/g, '\n');
const body = name => {
  const found = cppFunctionDefinitions(source).find(f => f.name === name);
  assert.ok(found, name);
  return found.source;
};
const tune = body('streamAutoTune');

assert.match(tune, /const bool forced =\s*static_cast<uint32_t>\(now_ms - run\.last_measured_ms\) >= kStreamTuneForceMs;\s*if \(forced\) \{\s*timeout_ms = static_cast<int>\(currentFrameMs\(\)\) \+ kStreamStatisticsMarginMs;[\s\S]*?\} else if \(timeout_ms < kStreamMinStatisticsWaitMs\) \{\s*return;\s*\}/,
  'an overdue read waits a whole frame, whatever the budget');
assert.doesNotMatch(tune, /run\.awb_next = !run\.awb_next;/, 'a failed read does not flip the kind');
const awbOk = tune.indexOf('run.awb_next = false;');
const aeOk = tune.indexOf('run.awb_next = true;');
assert.ok(awbOk > tune.indexOf('esp_isp_awb_controller_get_oneshot_statistics('), 'AWB counts only after its read');
assert.ok(aeOk > tune.indexOf('esp_isp_ae_controller_get_oneshot_statistics('), 'AE counts only after its read');
assert.equal(tune.split('if (!read) return;').length - 1, 2, 'a failed read changes nothing');
assert.equal(tune.split('run.last_measured_ms = now_ms;').length - 1, 2, 'both reads mark a measurement');
assert.match(body('streamSettle'), /run\.last_measured_ms = run\.last_tune_ms;/);
assert.match(source, /constexpr int kStreamStatisticsMarginMs = 10;/);

// Why the old wait never fit: currentFrameMs() for the OV02C10 stream limits.
const frameMs = lines => Math.ceil(34 * Math.max(lines, 1132) / 1132);
const oldMaxWait = 40;
assert.ok(frameMs(2328) > oldMaxWait, '15 fps frames are longer than the old maximum wait');
assert.ok(frameMs(1331) + 10 > oldMaxWait, 'a 25 fps frame plus margin exceeds it too');
console.log('Local camera stream: overdue statistics wait a whole frame, so the running stream measures again');
