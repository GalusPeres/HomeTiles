// V2 2026-10-03, b209 timing log: in a running stream 29 of 30 statistics
// reads timed out (a oneshot read waits for the start and the end of the next
// frame, up to 80 ms at 25 fps), and every wait cost a frame. The stream now
// switches the ISP to continuous statistics after its start: the ISP reports
// every frame to a callback, and the stream takes the latest values without
// waiting. The same log showed noisy 100 KB frames holding the stream at
// 15 fps behind the sender without a frame ever being replaced, so the
// quality never dropped; a sender still busy half a frame later now counts
// toward the quality drop too.
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';
import {cppFunctionDefinitions, maskCpp} from '../../lib/cpp-source.mjs';

const source = maskCpp(readRepoFile('src/video/local_camera/local_camera.cpp')).replace(/\r\n?/g, '\n');
const body = name => {
  const found = cppFunctionDefinitions(source).find(f => f.name === name);
  assert.ok(found, name);
  return found.source;
};
const before = (text, first, second, message) => {
  const a = text.indexOf(first);
  const b = text.indexOf(second);
  assert.ok(a >= 0 && b > a, message);
};

// Callbacks registered before the controllers are enabled (driver rule).
const ae = body('createAutoExposure');
before(ae, 'esp_isp_ae_env_detector_register_event_callbacks(', 'esp_isp_ae_controller_enable(',
  'AE callback before enable');
assert.match(source, /esp_isp_awb_register_event_callbacks\(g_pipe\.awb, &callbacks, nullptr\)/);
before(source, 'esp_isp_awb_register_event_callbacks(', 'esp_isp_awb_controller_enable(',
  'AWB callback before enable');
for (const name of ['onLiveAeStatistics', 'onLiveAwbStatistics']) {
  assert.match(body(name), /portENTER_CRITICAL_ISR\(&g_live_mux\);[\s\S]*portEXIT_CRITICAL_ISR\(&g_live_mux\);\s*return false;/, name);
}

// On after the stream start, off before snapshots and teardown.
const run = body('runStream');
before(run, 'reason = streamSettle(run, &leftover);', 'startLiveStatistics();', 'starts after the settle');
before(run, '} while (false);', 'stopLiveStatistics();', 'stops when the stream ends');
before(body('releasePipeline'), 'stopLiveStatistics();', 'esp_isp_ae_controller_disable(', 'off before disable');

// The running stream never waits for statistics while they are live.
assert.match(body('streamAutoTune'), /if \(g_live_ae_running\) \{\s*streamAutoTuneLive\(run\);\s*return;\s*\}/);
const live = body('streamAutoTuneLive');
assert.doesNotMatch(live, /oneshot_statistics|vTaskDelay|xQueueReceive/, 'no waiting');
assert.match(live, /ae_frames - run\.live_changed_at < kLiveSettleFrames/, 'only frames exposed after a change');
assert.match(live, /if \(!step\.converged && setExposureIfChanged\(step\.next\)\) run\.live_changed_at = ae_frames;/);

// A sender that falls behind lowers the quality too.
const capture = body('streamCaptureFrame');
assert.match(capture, /upload_behind = waited_ms \* 2 >= frame_ms;\s*if \(upload_behind\) noteUploadBusy\(run, false\);/);
before(capture, 'if (upload_behind) return;', 'run.busy_frames = 0;', 'a held-back frame is no calm frame');
assert.match(body('noteUploadBusy'), /if \(replaced\) \+\+run\.window\.busy;/);
console.log('Local camera stream: continuous statistics without waiting, quality follows a slow sender');
