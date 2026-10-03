// 8-inch 2026-10-03, b214 log: the ISP reports one AE result per frame
// (ae_results 255 in 10 s at 25 fps), yet the stream luma showed a gain
// change only 0.6-0.8 s later while the AE stepped every 0.2 s. Four steps
// down kept the luma at 122, then it fell to 107; the stream swung between
// 76 and 164. The stream settle phase saw the same sensor answer within
// 0.15 s, so a fixed settle time fits no device. A stream step now waits until
// it shows in the measured luma (or clearly the other way, a scene change)
// and two results in a row hold still, at most 1.5 s. White balance waits
// for the exposure, failed sensor writes are counted, and each window reports
// how long the steps took to show.
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';
import {cppFunctionDefinitions, maskCpp} from '../../lib/cpp-source.mjs';

const read = file => maskCpp(readRepoFile(file)).replace(/\r\n?/g, '\n');
const core = read('src/video/local_camera/local_camera.cpp');
const contract = read('src/video/local_camera/local_camera_contract.h');
const streamRaw = readRepoFile('src/video/local_camera/local_camera_stream_contract.h');
const body = (source, name) => {
  const found = cppFunctionDefinitions(source).find(f => f.name === name);
  assert.ok(found, name);
  return found.source;
};
const before = (text, first, second, message) => {
  const a = text.indexOf(first);
  const b = text.indexOf(second, a + 1);
  assert.ok(a >= 0 && b > a, message);
};

// The rule itself (values: test-local-camera-contract.mjs).
assert.match(contract, /enum class StepProgress : uint8_t \{ Waiting, Arrived, SceneChanged \};/);
assert.match(contract, /constexpr float kStepArrivedFraction = 0\.5f;/);
assert.ok(cppFunctionDefinitions(contract).some(f => f.name === 'stepProgress'));
assert.ok(cppFunctionDefinitions(contract).some(f => f.name === 'lumaSteady'));

// No fixed settle time is left in the stream.
assert.doesNotMatch(core, /kLiveSettleFrames/);
// b215 8-inch log: the sensor answers 2-3 frames after a write.
assert.match(core, /constexpr uint32_t kLiveMinSettleFrames = 3;/);
assert.match(core, /constexpr uint8_t kLiveConfirmResults = 2;/);
assert.match(core, /constexpr uint32_t kLiveMaxSettleMs = 600;/);

const settled = body(core, 'liveStepSettled');
before(settled, 'if (written_ms < kLiveMinSettleFrames * frame_ms) return false;',
  'stepProgress(run.live_from_luma, run.live_expected_luma, luma)', 'never before the sensor latency');
assert.match(settled, /progress != StepProgress::Waiting && steady \? run\.live_confirmed \+ 1 : 0;/);
assert.match(settled, /const bool shown = run\.live_confirmed >= kLiveConfirmResults;/);
assert.match(settled, /written_ms < std::max\(kLiveMaxSettleMs, kLiveMaxSettleFrames \* frame_ms\)/,
  'a step that never shows ends the wait');
for (const counter of ['++window.settles;', '++window.settle_timeouts;', '++window.scene_changes;']) {
  assert.ok(settled.includes(counter), counter);
}

const wait = body(core, 'startStepWait');
assert.match(wait, /run\.live_waiting = ratio < 0\.995f \|\| ratio > 1\.005f;/);
assert.match(wait, /powf\(ratio, aeExponent\(\)\)/, 'expected luma through the gamma curve');

const live = body(core, 'streamAutoTuneLive');
before(live, 'if (ae_frames == run.live_ae_seen) return;',
  'if (run.live_waiting && !liveStepSettled(run, luma, now_ms)) return;', 'every new result is checked');
before(live, 'liveStepSettled(run, luma, now_ms)', 'applyStreamExposureStep(run, ratio)', 'no step while waiting');
// The applied ratio, not the asked one: limits can cut a step short.
assert.match(live, /const float before = streamTargetProduct\(run\);\s*if \(applyStreamExposureStep\(run, ratio\)\)/);
assert.match(live, /startStepWait\(run, run\.mean_luma, after \/ before, now_ms\);/, 'a Max. gain change waits too');
assert.match(live, /if \(g_live_awb_running && !run\.live_waiting &&/, 'white balance waits for the exposure');

// A digital change glides from the curve on screen, so it lands within three frames.
const digital = body(core, 'setStreamDigital');
assert.match(digital, /const float from = g_gamma_applied_gain > 0\.0f \? g_gamma_applied_gain : g_gamma_curve_gain;/);
before(digital, 'loadGammaCurve(g_pipe.gamma_contrast, g_digital_step, true);',
  'g_gamma_ramp_per_frame = std::max(1.01f, cbrtf(change));', 'ramp from the new curve');

// Failed sensor writes are counted and reported per window.
assert.match(body(core, 'setExposureIfChanged'),
  /if \(g_sensor\.setExposure\(g_exposure\.lines, g_exposure\.gain_x16\) != ESP_OK\) \{\s*\+\+g_sensor_write_failures;/);
const diag = body(core, 'streamDiagnostics');
assert.match(diag, /run\.window\.sensor_fail = g_sensor_write_failures - run\.sensor_fail_seen;/);
assert.match(diag, /char timing\[512\];/, 'the longer timing line still fits');
for (const key of ['settles', 'settle_ms', 'settle_max', 'settle_timeouts', 'scene_changes', 'sensor_fail']) {
  assert.ok(streamRaw.includes(`\\"${key}\\":%u`), key);
}

// The per-result trace is beta-only and one line (a newline in a printf
// literal broke the first b209 build).
const trace = body(core, 'traceStepResult');
assert.match(trace, /#if defined\(HOMETILES_TEST_BETA\)/);
assert.match(trace, /if \(run\.live_wait_results > kLiveTraceResults\) return;/);
assert.ok(readRepoFile('src/video/local_camera/local_camera.cpp')
  .includes('Serial.printf("[LocalCam] AE f %u: %u ms, luma %u (from %u, want %u), gain %u/16, lines %u, digital %.2f/%.2f\\n",'));
console.log('Local camera stream: a step waits until it shows in the luma, any sensor delay');
