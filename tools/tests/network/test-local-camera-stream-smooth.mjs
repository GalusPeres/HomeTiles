// V2 2026-10-03 (b210): once the running stream measured every frame, the
// digital gain hunted between steps every few hundred ms. Each step reloaded
// the gamma curve mid-frame: the top of the picture kept the old curve and
// the rest took the new one, a hard cut with a short stutter. Now curve and
// colour changes wait for the next frame end, and the stream keeps a wider
// band and moves in small steps unless the light really changed.
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
  const b = text.indexOf(second, a + 1);
  assert.ok(a >= 0 && b > a, message);
};

// Held back while a stream runs, written at a frame end.
assert.match(body('applyColorCorrection'), /if \(g_defer_isp_updates\) \{\s*g_ccm_pending = true;\s*return;\s*\}/);
assert.match(body('loadGammaCurve'), /const esp_err_t err = g_defer_isp_updates \? ESP_OK : writeGammaCurve\(\);/);
assert.match(body('loadGammaCurve'), /g_gamma_pending = g_defer_isp_updates;/);
const pending = body('applyPendingIspUpdates');
assert.match(pending, /writeGammaCurve\(\)/);
assert.match(pending, /writeColorCorrection\(\)/);
const capture = body('streamCaptureFrame');
before(capture, '++window.noframe;', 'applyPendingIspUpdates();', 'only after a frame was frozen');
before(capture, 'applyPendingIspUpdates();', 'Dma2dArbiterGuard guard', 'before the encoder runs');
const run = body('runStream');
before(run, 'startLiveStatistics();', 'g_defer_isp_updates = true;', 'held back from the stream start');
before(run, 'stopLiveStatistics();', 'g_defer_isp_updates = false;', 'released at the stream end');
before(run, 'g_defer_isp_updates = false;', 'applyPendingIspUpdates();', 'and flushed');
assert.match(body('releasePipeline'), /g_defer_isp_updates = false;\s*g_gamma_pending = false;\s*g_ccm_pending = false;/);

// The running stream regulates like Espressif's esp_ipa tuning for the P4
// sensors (b213, after the V2 and 8-inch recordings): 32 % of the way
// brighter and 42 % darker, no step below 3 %, a +-6 % hold band, and
// libcamera's fast reduce: below 60 % of the needed exposure the correction
// lands at once. A step follows once the last one shows in the luma
// (test-local-camera-stream-step-arrival.mjs).
assert.match(source, /constexpr float kStreamRiseSpeed = 0\.32f;/);
assert.match(source, /constexpr float kStreamFallSpeed = 0\.42f;/);
assert.match(source, /constexpr float kStreamMinStep = 0\.03f;/);
assert.match(source, /constexpr uint32_t kStreamHoldPercent = 6;/);
assert.match(source, /constexpr float kStreamFastReduceRatio = 0\.6f;/);
const live = body('streamAutoTuneLive');
assert.match(live, /if \(run\.mean_luma \+ hold >= target && run\.mean_luma <= target \+ hold\) return;/);
assert.match(live, /float ratio = needed < kStreamFastReduceRatio\s*\? needed\s*: powf\(needed, needed > 1\.0f \? kStreamRiseSpeed : kStreamFallSpeed\);/);
assert.match(live, /if \(ratio > 1\.0f && ratio < 1\.0f \+ kStreamMinStep\) ratio = 1\.0f \+ kStreamMinStep;/);
assert.doesNotMatch(live, /stepDigitalGain/, 'the stream digital gain is stepless');
// Darker: digital gain first; brighter: the sensor first, then the digital gain.
const apply = body('applyStreamExposureStep');
before(apply, 'if (rest < 1.0f && g_stream_digital > 1.0f)', 'scaleStagedExposure(g_exposure, rest, run.stages)',
  'darker takes the digital gain down first');
before(apply, 'scaleStagedExposure(g_exposure, rest, run.stages)', 'setStreamDigital(g_stream_digital * rest);',
  'brighter raises the sensor first');
// A digital change glides over at most three frames.
assert.match(body('setStreamDigital'), /g_gamma_ramp_per_frame = std::max\(1\.01f, cbrtf\(change\)\);/);
const ramp = body('applyPendingIspUpdates');
assert.match(ramp, /if \(ratio > g_gamma_ramp_per_frame\) next = g_gamma_applied_gain \* g_gamma_ramp_per_frame;/);
assert.match(ramp, /g_gamma_pending = next != g_gamma_curve_gain;/);
before(run, 'g_gamma_gentle = false;', 'applyPendingIspUpdates();', 'the rest of a glide lands at the stream end');
before(run, 'applyPendingIspUpdates();', 'g_stream_digital = 0.0f;', 'snapshots use quarter-EV steps again');
console.log('Local camera stream: curve changes at frame ends, gentle steps within a wider band');
