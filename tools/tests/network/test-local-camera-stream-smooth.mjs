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

// Wider band and small steps in the running stream.
assert.match(source, /constexpr uint32_t kStreamAeTolerance = 24;/);
assert.match(source, /constexpr float kStreamGentleRatio = 1\.25f;/);
const live = body('streamAutoTuneLive');
assert.match(live, /const bool far = run\.mean_luma \* 2 < target \|\| run\.mean_luma \* 2 > target \* 3;/);
assert.match(live, /far \? kMaxExposureRatio : kStreamGentleRatio/);
assert.match(live, /stepDigitalGain\(run\.mean_luma, step\.limited, kStreamAeTolerance,\s*far \? kMaxDigitalGainJump : 1\)/);
console.log('Local camera stream: curve changes at frame ends, gentle steps within a wider band');
