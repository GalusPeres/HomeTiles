// #116: the camera popup ended after 18-60 minutes with "Stream connection
// ended"; the panel log showed only a lower frame rate before the end. The
// stream task now logs a wait of a second or more for the Bridge's bytes
// (rate-limited), the end line says how long ago data last arrived, and the UI
// task logs the Wi-Fi signal once after a stream ended on its own. The radio
// query stays off the camera task (on the P4 it is an ESP-Hosted call).
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const source = fs.readFileSync(path.join(root, 'src/video/camera_stream.cpp'), 'utf8')
  .replaceAll('\r\n', '\n');

function bodyOf(name) {
  const found = cppFunctionDefinitions(source).find(item => item.name === name);
  assert.ok(found, `${name} must exist`);
  return found.body;
}

assert.match(source, /constexpr uint32_t kDataGapLogMs = 1000;/);
assert.match(source, /constexpr uint32_t kDataGapLogIntervalMs = 10000;/);

const task = bodyOf('run_camera_task');
// Every byte of the stream goes through the timed receive.
assert.equal((task.match(/socket_receive_exact\(/g) || []).length, 2,
  'only the handshake and the timed receive call the socket directly');
assert.match(task, /receive_data\(header, sizeof\(header\)\)/);
assert.match(task, /receive_data\(input \+ received_bytes, receive_now\)/);
assert.match(task,
  /if \(waited_ms < kDataGapLogMs\) return result;\s*if \(gap_logged_ms != 0 && now - gap_logged_ms < kDataGapLogIntervalMs\) \{\s*\+\+gaps_not_logged;/,
  'gaps of a second or more are logged at most once per interval, the rest counted');
assert.match(task, /"\[CameraStream\] No data for %u ms \(%u more gaps since the last line\)\\n"/);
assert.match(task,
  /TCP ACK stream ended: ok=%s, last data %u ms ago\\n",\s*stream_ok \? "yes" : "no",\s*static_cast<unsigned>\(millis\(\) - last_data_ms\)\);/);
assert.match(task, /\} else \{\s*g_log_wifi_at_end = true;/,
  'only a stream that ended on its own asks for the Wi-Fi line');
assert.doesNotMatch(task, /WiFi\./, 'the camera task never queries the radio');

const wifi = bodyOf('log_wifi_at_stream_end');
assert.match(wifi, /if \(!g_log_wifi_at_end\) return;\s*g_log_wifi_at_end = false;/);
assert.match(wifi, /networkTransport\.activeKind\(\) != NetworkTransportKind::Wifi\) return;/);
assert.match(wifi, /Wi-Fi at stream end: RSSI %d dBm/);
assert.match(bodyOf('camera_stream_process_ui'), /^\{\s*log_wifi_at_stream_end\(\);/);
assert.match(bodyOf('camera_stream_start'), /g_log_wifi_at_end = false;/);

console.log('PASS: camera stream logs data gaps, the last data age and the Wi-Fi signal at its end');
