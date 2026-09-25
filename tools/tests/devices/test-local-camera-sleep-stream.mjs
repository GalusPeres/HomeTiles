// Built-in camera live stream during display sleep (maintainer decision
// 2026-09-25). Before, display sleep stopped the upload and deferred every
// keepalive, so Home Assistant only got still images while the display slept.
// Now the stream also runs while the display sleeps:
//   - indicator enabled (Web Admin "indicator" 1 = line, 2 = pill): a wanted or
//     running stream wakes the display from the loop task and keeps it awake,
//     so the indicator is always visible; when the stream ends the idle timer
//     restarts once and normal idle timing resumes;
//   - indicator off (0 = none): the stream runs while the display stays dark;
//   - still images never wake the display.
// The decision is host-tested through the shared contract header; the wiring
// is checked in the firmware sources.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions, maskCpp} from '../../lib/cpp-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = relative => fs.readFileSync(path.join(root, relative), 'utf8').replaceAll('\r\n', '\n');
const service = read('src/video/local_camera/local_camera.cpp');
const api = read('src/video/local_camera/local_camera.h');
const contract = read('src/video/local_camera/local_camera_stream_contract.h');
const power = read('src/core/power/power_manager.cpp');
const sketch = read('HomeTiles.ino');

function bodyOf(source, name) {
  const found = cppFunctionDefinitions(source).find(item => item.name === name);
  assert.ok(found, `${name} must exist`);
  return found.body;
}
const svc = name => bodyOf(service, name);

// --- The gate no longer knows display sleep ------------------------------------------
{
  const start = contract.indexOf('struct GateInputs {');
  assert.ok(start > 0);
  const gateInputs = contract.slice(start, contract.indexOf('};', start));
  assert.doesNotMatch(gateInputs, /sleep/i, 'GateInputs has no display sleep input');
}
assert.doesNotMatch(bodyOf(contract, 'streamGate'), /sleep|StopReason::Sleep/i);
assert.doesNotMatch(svc('currentGate'), /isInSleep/);

// --- Loop-task display service: stream flags only, wake on the loop task -----------------
const display = svc('serviceStreamDisplay');
assert.match(display, /const bool stream_active = g_stream_wanted\.load\(\) \|\| g_stream_running\.load\(\);/);
assert.match(display, /g_indicator_style\.load\(\) != static_cast<uint8_t>\(IndicatorStyle::None\);/,
  'Only the "none" style keeps the display dark');
assert.match(display, /streamDisplayStep\(\s*stream_active, indicator_enabled, powerManager\.isInSleep\(\), &g_display_kept_awake\);/);
assert.match(display, /if \(action\.wake\) powerManager\.wakeFromDisplaySleep\("camera"\);/);
assert.match(display, /if \(action\.reset_activity\) displayManager\.resetActivityTimer\(\);/);
assert.doesNotMatch(display, /g_sensor_capturing|indicatorActive|g_capture_busy|g_pending|blockSleep|allowSleep/,
  'Still images (sensor capture, snapshot requests) never wake the display; no shared sleep block');
assert.match(api, /enum class IndicatorStyle : uint8_t \{ None = 0, Line = 1, Pill = 2 \};/,
  'Web Admin indicator 0 means off');
assert.match(svc('service'), /serviceStream\(\);\s*serviceStreamDisplay\(\);/);
// The worker never touches the display: exactly one wake call, in the loop service.
const masked = maskCpp(service);
assert.equal([...masked.matchAll(/wakeFromDisplaySleep\(/g)].length, 1);
assert.equal([...masked.matchAll(/resetActivityTimer\(/g)].length, 1);
for (const worker of ['workerMain', 'runStream', 'runCapture', 'streamWait']) {
  assert.doesNotMatch(svc(worker), /wakeFromDisplaySleep|resetActivityTimer|serviceStreamDisplay/,
    `${worker} runs on the camera worker`);
}
// Only a keepalive starts the stream; a still request never sets the stream flag.
assert.equal([...masked.matchAll(/g_stream_wanted\.store\(true\)/g)].length, 1);
assert.match(svc('tryStartStream'), /g_stream_wanted\.store\(true\);/);

// The sleep loop keeps servicing the camera, so a keepalive that arrives during
// sleep starts the stream and the same pass can wake the display.
{
  const sleepStart = sketch.indexOf('if (powerManager.isInSleep()) {');
  const sleepEnd = sketch.indexOf('// Resume active mode.');
  assert.ok(sleepStart > 0 && sleepEnd > sleepStart);
  const sleepLoop = sketch.slice(sleepStart, sleepEnd);
  assert.match(sleepLoop, /mqtt_process_inbound_queue\(\);\s*local_camera::service\(\);/);
  assert.match(sketch.slice(sleepEnd), /local_camera::service\(\);/, 'Awake loop too');
}

// --- Sleep release frees only an idle camera -----------------------------------------------
assert.match(power, /is_display_sleeping = true;[\s\S]*?local_camera::releaseForSleep\(\);/);
assert.match(svc('releaseForSleep'),
  /if \(g_stream_wanted\.load\(\) \|\| g_stream_running\.load\(\)\) return;\s*notifyWorker\(kNotifyRelease\);/);
// The wake path restarts the idle timer (display awake for the full timeout).
assert.match(bodyOf(power, 'PowerManager::wakeFromDisplaySleep'), /displayManager\.resetActivityTimer\(\);/);

// --- Host: gate and display decision --------------------------------------------------------
function findCompiler() {
  for (const candidate of [process.env.CXX, 'clang++', 'g++', 'c++'].filter(Boolean)) {
    const check = spawnSync(candidate, ['--version'], {encoding: 'utf8'});
    if (!check.error && check.status === 0) return candidate;
  }
  return null;
}

const compiler = findCompiler();
if (!compiler) {
  console.log('SKIP: local camera sleep stream host check needs a host C++ compiler');
  process.exit(0);
}

const program = String.raw`
#include "src/video/local_camera/local_camera_stream_contract.h"
#include <cassert>
#include <cstdio>
#include <type_traits>
#include <utility>

using namespace local_camera_stream;

template <class T, class = void>
struct HasDisplaySleeping : std::false_type {};
template <class T>
struct HasDisplaySleeping<T, std::void_t<decltype(std::declval<T>().display_sleeping)>>
    : std::true_type {};
static_assert(!HasDisplaySleeping<GateInputs>::value, "display sleep must not gate the stream");

int main() {
  // A valid session streams; nothing about the display can stop it.
  GateInputs in;
  in.session_valid = true;
  in.enabled = true;
  in.sensor_ready = true;
  in.mqtt_connected = true;
  assert(streamGate(in) == StopReason::None);

  // Indicator on, display asleep: the stream start wakes it and keeps it awake.
  {
    bool kept = false;
    StreamDisplayAction a = streamDisplayStep(true, true, true, &kept);
    assert(a.wake && a.reset_activity && kept);
    // Awake while the stream runs: no second wake, idle timer held every pass.
    for (int pass = 0; pass < 3; ++pass) {
      a = streamDisplayStep(true, true, false, &kept);
      assert(!a.wake && a.reset_activity && kept);
    }
    // Put to sleep while streaming (e.g. a sleep command): woken again.
    a = streamDisplayStep(true, true, true, &kept);
    assert(a.wake && a.reset_activity);
    // Stream ends: the idle timer restarts once, then normal idle timing.
    a = streamDisplayStep(false, true, false, &kept);
    assert(!a.wake && a.reset_activity && !kept);
    a = streamDisplayStep(false, true, false, &kept);
    assert(!a.wake && !a.reset_activity && !kept);
  }

  // Indicator off: the stream runs, the display stays dark, nothing is reset.
  {
    bool kept = false;
    for (int pass = 0; pass < 3; ++pass) {
      const StreamDisplayAction a = streamDisplayStep(true, false, true, &kept);
      assert(!a.wake && !a.reset_activity && !kept);
    }
    // Awake with the indicator off: normal idle timing, the display may sleep.
    const StreamDisplayAction a = streamDisplayStep(true, false, false, &kept);
    assert(!a.wake && !a.reset_activity && !kept);
  }

  // Indicator switched off mid-stream: one idle timer restart, then idle.
  {
    bool kept = false;
    streamDisplayStep(true, true, false, &kept);
    StreamDisplayAction a = streamDisplayStep(true, false, false, &kept);
    assert(!a.wake && a.reset_activity && !kept);
    a = streamDisplayStep(true, false, true, &kept);
    assert(!a.wake && !a.reset_activity);
    // Switched on again while the display sleeps and the stream runs: wake.
    a = streamDisplayStep(true, true, true, &kept);
    assert(a.wake && a.reset_activity && kept);
  }

  // No stream (still images only): never a wake, never a reset.
  for (int indicator = 0; indicator < 2; ++indicator) {
    for (int sleeping = 0; sleeping < 2; ++sleeping) {
      bool kept = false;
      const StreamDisplayAction a = streamDisplayStep(false, indicator != 0, sleeping != 0, &kept);
      assert(!a.wake && !a.reset_activity && !kept);
    }
  }

  // Without state the decision still holds (no edge restart).
  const StreamDisplayAction stateless = streamDisplayStep(true, true, true, nullptr);
  assert(stateless.wake && stateless.reset_activity);

  std::puts("ok");
  return 0;
}
`;

const out = path.join(root, 'build/tests/local-camera-sleep-stream');
fs.mkdirSync(out, {recursive: true});
const cpp = path.join(out, 'sleep_stream.cpp');
const exe = path.join(out, process.platform === 'win32' ? 'sleep_stream.exe' : 'sleep_stream');
fs.writeFileSync(cpp, program);
const build = spawnSync(compiler, ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', root, cpp, '-o', exe],
  {encoding: 'utf8'});
assert.equal(build.status, 0, `sleep stream check did not compile:\n${build.stdout}${build.stderr}`);
const run = spawnSync(exe, [], {encoding: 'utf8'});
assert.equal(run.status, 0, `sleep stream check failed:\n${run.stdout}${run.stderr}`);
assert.match(run.stdout, /^ok/m);

console.log('Local camera stream during display sleep: gate, indicator wake/keep-awake and still images passed.');
