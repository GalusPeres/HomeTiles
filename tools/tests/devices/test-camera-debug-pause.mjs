// #116: a Wi-Fi stall ended the camera popup (the Bridge gave up after 5 s
// without an acknowledgement; Bridge v0.9.4b5 waits 45 s). Switching a
// network off to test that is no option, so test builds get
// /api/debug/camera-pause: the stream task then reads and acknowledges nothing
// for a while, exactly what the Bridge sees during a stall. Release builds do
// not register the route.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = relative => fs.readFileSync(path.join(root, relative), 'utf8').replaceAll('\r\n', '\n');
const stream = read('src/video/camera_stream.cpp');
const web = read('src/web/server/web_admin.cpp');

const route = web.match(/#if defined\(HOMETILES_BETA_BUILD\)\n([\s\S]*?)#endif/g)
  ?.find(block => block.includes('/api/debug/camera-pause'));
assert.ok(route, 'the route exists only in test builds');
assert.match(route, /server\.on\("\/api\/debug\/camera-pause", HTTP_GET, guarded\(/,
  'the Web Admin password protects it like every other API');
assert.match(route, /if \(seconds < 1\) seconds = 1;\s*if \(seconds > 60\) seconds = 60;/);
assert.match(route, /camera_stream_debug_pause\(static_cast<uint32_t>\(seconds\) \* 1000U\)/);

const task = cppFunctionDefinitions(stream).find(item => item.name === 'run_camera_task').body;
assert.match(task,
  /if \(const uint32_t pause_ms = g_debug_pause_ms\) \{[\s\S]*?while \(!g_stop_requested && millis\(\) - paused_at < pause_ms\) \{\s*vTaskDelay\(pdMS_TO_TICKS\(50\)\);\s*\}[\s\S]*?\}\s*uint8_t header\[kCameraFrameHeaderBytes\] = \{\};/,
  'the pause sits before the next frame: nothing is read or acknowledged meanwhile');

const definitions = cppFunctionDefinitions(stream).filter(item => item.name === 'camera_stream_debug_pause');
assert.equal(definitions.length, 2, 'a real one and the stub for panels without the camera stream');
assert.match(definitions[0].body, /if \(!task_is_active\(\) \|\| ms == 0\) return false;\s*g_debug_pause_ms = ms;\s*return true;/);

console.log('PASS: test builds can hold the camera stream to reproduce a Wi-Fi stall');
