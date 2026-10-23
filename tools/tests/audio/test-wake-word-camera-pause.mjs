// 2026-10-21, Guition V2: the camera full screen ran at 21-23 FPS (13 Mbit/s)
// with the wake word "alexa" listening and at 30 FPS (15.5 Mbit/s) right
// after "[Speaker] Wake words: off". The listener runs on core 0 at priority
// 3, the camera stream task there at idle priority, so listening delayed the
// stream's acknowledgements. The UI on core 1 must stay smooth, so the wake
// word does not move there: a camera stream pauses listening instead, and it
// resumes two seconds after the last stream (a popup/full-screen switch keeps
// the microphone closed).
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const speaker = fs.readFileSync(path.join(root, 'src/audio/speaker.cpp'), 'utf8')
  .replaceAll('\r\n', '\n');

function bodyOf(name) {
  const found = cppFunctionDefinitions(speaker).find(item => item.name === name);
  assert.ok(found, `${name} must exist`);
  return found.body;
}

assert.match(speaker, /#include "src\/video\/camera_stream\.h"/);
assert.match(speaker, /constexpr uint32_t kWakeCameraHoldMs = 2000;/);
assert.match(bodyOf('cameraHoldsMicrophone'),
  /if \(camera_stream_is_active\(\)\) \{\s*g_camera_seen_ms = now \? now : 1;\s*return true;\s*\}\s*return g_camera_seen_ms != 0 &&\s*static_cast<uint32_t>\(now - g_camera_seen_ms\) < kWakeCameraHoldMs;/,
  'a stream holds the microphone, and for two seconds after it');

const wanted = bodyOf('wakeListenWanted');
assert.match(wanted, /if \(!wakeListenConfigured\(\)\) \{\s*g_wake_camera_paused = false;\s*return false;\s*\}/);
assert.match(wanted, /const bool paused = cameraHoldsMicrophone\(\);[\s\S]*return !paused;/);
assert.match(wanted, /"paused while a camera stream runs"\s*:\s*"listening again after the camera stream"/,
  'one log line per change, not per poll');

assert.match(bodyOf('wakeListen'),
  /if \(bits \|\| g_shutdown\.load\(\) \|\| activeWakeWords\(\) != mask \|\| camera_stream_is_active\(\)\) \{\s*break;\s*\}/,
  'a stream that starts closes the microphone at the next 20 ms chunk');

assert.match(bodyOf('workerMain'),
  /\} else if \(g_wake_camera_paused\) \{\s*wait = pdMS_TO_TICKS\(kWakeCameraPollMs\);/,
  'the paused worker looks again instead of waiting forever for a notification');

console.log('PASS: a camera stream pauses the wake word, which listens again after it');
