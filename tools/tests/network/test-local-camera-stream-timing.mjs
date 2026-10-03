// V2 2026-10-03: bright, noisy frames dropped the stream from 25 to 15 fps,
// and the window statistics did not say where a frame interval went. Each
// window now also records the waits for the sender, for a fresh CSI frame and
// for the exposure / white balance statistics, plus the longest pass; they go
// to the log line "[LocalCamStream] timing" and /api/local-camera
// "stream_timing".
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';
import {cppFunctionDefinitions, maskCpp} from '../../lib/cpp-source.mjs';

const read = file => maskCpp(readRepoFile(file)).replace(/\r\n?/g, '\n');
const contract = read('src/video/local_camera/local_camera_stream_contract.h');
const core = read('src/video/local_camera/local_camera.cpp');
const body = (source, name) => {
  const found = cppFunctionDefinitions(source).find(f => f.name === name);
  assert.ok(found, name);
  return found.source;
};

for (const field of ['sender_wait_ms_max', 'freeze_ms_max', 'tune_ok', 'tune_timeout', 'tune_forced',
                     'tune_ms_max', 'loop_ms_max']) {
  assert.match(contract, new RegExp(`uint32_t ${field} = 0;`), field);
}
assert.ok(cppFunctionDefinitions(contract).some(f => f.name === 'formatTimingJson'));

const capture = body(core, 'streamCaptureFrame');
assert.match(capture, /\+\+window\.sender_waits;/);
assert.match(capture, /\+\+window\.freezes;/);
const tune = body(core, 'streamAutoTune');
assert.equal(tune.split('noteStatisticsRead(run.window, now_ms, read);').length - 1, 2, 'AE and AWB reads are counted');
assert.match(tune, /\+\+run\.window\.tune_forced;/);
assert.match(body(core, 'runStream'), /if \(pass_ms > run\.window\.loop_ms_max\) run\.window\.loop_ms_max = pass_ms;/);
assert.match(body(core, 'streamDiagnostics'), /formatTimingJson\(timing, sizeof\(timing\), run\.window\)/);
assert.match(body(core, 'appendStatusJson'), /formatTimingJson\(stream_json, sizeof\(stream_json\), stream\.window\)/);
// One line: a newline written into this literal broke the first b209 build.
assert.ok(readRepoFile('src/video/local_camera/local_camera.cpp')
  .includes('Serial.printf("[LocalCamStream] timing %s\\n", timing);'));
console.log('Local camera stream: sender, frame and statistics waits are logged per window');
