// 8-inch 2026-10-03, b215: the stream regulation no longer swung, but each
// sensor step was written at once and showed as a jump. Five damped steps
// from dark to the target looked like five brightness levels. A sensor step
// now glides over four frames, one write per frame (the exposure product
// moves by an equal factor, split from the start setting, the last part is
// the target), and the AE measures again only three frames after the last
// write. The host simulation cut the largest frame-to-frame jump while
// brightening from 10-20 to 4-7 luma levels without any reversal.
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';
import {cppFunctionDefinitions, maskCpp} from '../../lib/cpp-source.mjs';

const core = maskCpp(readRepoFile('src/video/local_camera/local_camera.cpp')).replace(/\r\n?/g, '\n');
const body = name => {
  const found = cppFunctionDefinitions(core).find(f => f.name === name);
  assert.ok(found, name);
  return found.source;
};
const before = (text, first, second, message) => {
  const a = text.indexOf(first);
  const b = text.indexOf(second, a + 1);
  assert.ok(a >= 0 && b > a, message);
};

assert.match(core, /constexpr uint8_t kSensorGlideFrames = 4;/);

// The step starts a glide instead of writing the whole change.
const apply = body('applyStreamExposureStep');
assert.doesNotMatch(apply, /setExposureIfChanged\(step\.next\)/, 'no single jump');
before(apply, 'const ExposureStep step = scaleStagedExposure(g_exposure, rest, run.stages);',
  'startSensorGlide(run, step.next);', 'glides to the staged target');
assert.match(apply, /const float after = static_cast<float>\(step\.next\.lines\) \* step\.next\.gain_x16;/,
  'the digital rest follows the target, not the first part');

const start = body('startSensorGlide');
assert.match(start, /run\.glide_from = g_exposure;\s*run\.glide_to = next;/);
assert.match(start, /advanceSensorGlide\(run, millis\(\)\);/, 'the first part is written at once');

const glide = body('advanceSensorGlide');
assert.match(glide, /if \(run\.glide_done >= kSensorGlideFrames\) \{\s*setExposureIfChanged\(run\.glide_to\);/,
  'the last part is the target exactly');
assert.match(glide, /powf\(to \/ from, static_cast<float>\(run\.glide_done\) \/ kSensorGlideFrames\)/);
assert.match(glide, /scaleStagedExposure\(run\.glide_from, part, run\.stages\)\.next/, 'no rounding drift');
assert.match(glide, /run\.live_written_ms = now_ms;/);

// One part per AE result (one per frame); no measurement while gliding.
const live = body('streamAutoTuneLive');
before(live, 'run.live_ae_seen = ae_frames;', 'advanceSensorGlide(run, now_ms);', 'once per new result');
before(live, 'advanceSensorGlide(run, now_ms);', 'liveStepSettled(run, luma, now_ms)', 'glide before the check');
assert.match(live, /if \(run\.gliding\) return;/);
assert.match(body('liveStepSettled'), /if \(run\.gliding\) return false;/);
// The expected luma uses the glide target.
assert.match(body('streamTargetProduct'), /run\.gliding \? run\.glide_to : g_exposure/);
console.log('Local camera stream: a sensor step glides over four frames');
