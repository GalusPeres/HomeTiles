// Lip sync for a camera with sound (b506, Bridge b13). b504/b505 played a
// camera's sound beside its picture, and the sound ran audibly behind it: the
// picture went to the screen at once, the sound waited in the panel's buffer
// (120-600 ms plus the I2S buffer). Now each picture waits as long as the
// panel's sound still needs, less how much earlier the Bridge sent the sound
// (lead_ms) and less the picture's own decode and present time.
//
// Checks the delay line (src/video/frame_delay_line.h) on the host, the
// speaker's delay report and the camera task's use of it: without sound the
// camera path stays exactly as before.
//
// b507: b506 took a held frame out only when the next one arrived and then
// showed just the newest due frame, so two frames that arrived together cost
// one (3-5 of 30 per second on the V2, 25-27 FPS). Now the task waits on the
// socket only until the next frame is due and shows due frames in order.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replaceAll('\r\n', '\n');

const camera = read('src/video/camera_stream.cpp');
const loop = camera.slice(camera.indexOf('const uint32_t picture_delay ='),
                          camera.indexOf('if (!presented_ok) {'));
assert.ok(loop.length > 0, 'the camera task presents frames here');
assert.match(loop, /const uint32_t picture_delay = camera_sync::pictureDelayMs\(speaker::cameraSyncDelayMs\(\)\);/);
assert.match(loop, /if \(picture_delay == 0 && delay_line\.empty\(\)\) \{\s*presented_ok = present_frame\(full, input, jpeg_bytes\);/,
             'without sound every frame goes out at once, as before');
assert.match(loop, /delay_line\.commit\(offset, jpeg_bytes, millis\(\), picture_delay\);\s*presented_ok = present_due_frames\(\);/);
// b508: the shortest and longest gap between frames on screen, in both FPS
// lines, so even spacing can be read from the log.
assert.match(camera, /static void note_presented_frame\(\) \{\s*\+\+g_presented_frame_count;\s*const uint32_t now = millis\(\);\s*g_present_gaps\.note\(now\);/);
assert.match(camera, /"\[CameraStream\] Present: %\.1f FPS wait=%\.1fms ppa\+cache\+swap=%\.1fms "\s*"gap=%u-%ums\\n"/);
assert.match(camera, /g_full_gaps\.note\(millis\(\)\);\s*\+\+g_full_frames;/);
assert.match(camera, /"\[CameraStream\] Full screen: %\.1f FPS decode=%\.1fms swap_wait=%\.1fms failed=%u "\s*"gap=%u-%ums\\n"/);
assert.match(camera, /g_present_gaps\.restart\(\);\s*g_full_gaps\.restart\(\);/, 'a new stream starts its gaps afresh');
assert.match(camera, /auto present_due_frames = \[&\]\(\) -> bool \{\s*camera_sync::DelayedFrame frame;\s*while \(delay_line\.takeDue\(millis\(\), &frame, &sync_skipped\)\) \{\s*if \(!present_frame\(full, delay_memory \+ frame\.offset, frame\.length\)\) return false;/,
             'every due frame is shown, oldest first');
const top = camera.slice(camera.indexOf('Serial.println("[CameraStream] Debug pause over, reading again");'),
                         camera.indexOf('uint8_t header[kCameraFrameHeaderBytes] = {};'));
assert.match(top, /if \(!delay_line\.empty\(\)\) \{[\s\S]*stream_ok = present_due_frames\(\);[\s\S]*delay_line\.nextDue\(&due_ms\)[\s\S]*!tcp_ack::waitReadable\(socket_fd, static_cast<uint32_t>\(wait_ms\)\)\) \{\s*continue;/,
             'a held frame goes out on time even while no new frame arrives');
assert.match(camera, /heap_caps_malloc\(\s*camera_sync::kDelayRingBytes, MALLOC_CAP_SPIRAM \| MALLOC_CAP_8BIT\)/,
             'the waiting pictures sit in PSRAM, never in internal RAM');
assert.match(camera, /heap_caps_free\(delay_memory\);\s*Serial\.printf\("\[CameraStream\] TCP ACK stream ended/);

const speaker = read('src/audio/speaker.cpp');
assert.match(speaker, /const uint64_t queued = src->buffer\.buffered\(\) \/ 2 \+ kLiveDmaDescriptors \* kLiveDmaFrames;\s*g_live_latency_ms\.store\(static_cast<uint32_t>\(queued \* 1000u \/ rate\) \+ 1\);/,
             'the live sound delay counts the buffer and the whole I2S buffer');
assert.match(speaker, /if \(live\) g_live_latency_ms\.store\(0\);/, 'no delay once the camera sound ends');
assert.match(speaker, /g_live_lead_ms\.store\(request\.live \? request\.lead_ms : 0\);/);
assert.match(speaker, /uint32_t cameraSyncDelayMs\(\) \{\s*const uint32_t latency = g_live_latency_ms\.load\(\);\s*const uint32_t lead = g_live_lead_ms\.load\(\);\s*return latency > lead \? latency - lead : 0;/);

function findCompiler() {
  for (const candidate of [process.env.CXX, 'clang++', 'g++', 'c++'].filter(Boolean)) {
    const check = spawnSync(candidate, ['--version'], {encoding: 'utf8'});
    if (!check.error && check.status === 0) return candidate;
  }
  return null;
}

const compiler = findCompiler();
if (!compiler) {
  console.log('SKIP: camera lip sync test needs a host C++ compiler');
  process.exit(0);
}

const program = String.raw`
#include "src/video/frame_delay_line.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace camera_sync;

int main() {
  // The picture's own decode and present time comes off; never negative,
  // never beyond the bound.
  assert(pictureDelayMs(0) == 0 && pictureDelayMs(kPresentMs) == 0);
  assert(pictureDelayMs(200) == 200 - kPresentMs);
  assert(pictureDelayMs(5000) == kMaxDelayMs);

  // A camera at 30 FPS, 60 KB frames, a 200 ms wait: the line shows each
  // frame once it is due, in order, and wraps in its ring.
  FrameDelayLine line;
  line.reset(kDelayRingBytes);
  std::vector<uint8_t> ring(kDelayRingBytes);
  uint32_t now = 1000, shown = 0, skipped = 0, last_shown = 0;
  for (uint32_t n = 1; n <= 600; ++n, now += 33) {
    const size_t length = 50000 + (n * 997) % 20000;
    size_t offset = 0;
    while (!line.reserve(length, &offset)) line.dropOldest();
    assert(offset + length <= kDelayRingBytes);
    memset(ring.data() + offset, static_cast<int>(n & 0xFF), length);
    line.commit(offset, length, now, 200);
    DelayedFrame frame;
    if (line.takeDue(now, &frame, &skipped)) {
      // The frame's bytes are intact (no later frame overwrote them).
      assert(ring[frame.offset] == ring[frame.offset + frame.length - 1]);
      const uint32_t id = ring[frame.offset];
      assert(id == ((last_shown + 1) & 0xFF));
      last_shown = id;
      assert(static_cast<int32_t>(now - frame.due_ms) >= 0 && now - frame.due_ms < 33);
      ++shown;
    }
    assert(line.size() <= 8);
  }
  assert(skipped == 0 && shown >= 590);

  // Wi-Fi hands frames over in pairs (two at once every 66 ms) and the task
  // wakes every 33 ms: every frame is shown, in order (b506 dropped one of
  // each pair here).
  line.reset(kDelayRingBytes);
  now = 1000; shown = 0; skipped = 0; last_shown = 0;
  uint32_t next_id = 1;
  for (uint32_t tick = 0; tick < 600; ++tick, now += 33) {
    if (tick % 2 == 0) {
      for (int pair = 0; pair < 2; ++pair, ++next_id) {
        size_t offset = 0;
        while (!line.reserve(60000, &offset)) line.dropOldest();
        memset(ring.data() + offset, static_cast<int>(next_id & 0xFF), 60000);
        line.commit(offset, 60000, now, 50);
      }
    }
    DelayedFrame frame;
    while (line.takeDue(now, &frame, &skipped)) {
      const uint32_t id = ring[frame.offset];
      assert(id == ((last_shown + 1) & 0xFF));
      last_shown = id;
      ++shown;
    }
    uint32_t due = 0;
    assert(!line.nextDue(&due) || static_cast<int32_t>(due - now) > 0);
  }
  assert(skipped == 0 && shown >= 590);

  // The sound's delay shrinks (or ends): frames catch up in order, they never
  // overtake an older one.
  line.reset(kDelayRingBytes);
  size_t offset = 0;
  assert(line.reserve(1000, &offset) && offset == 0);
  line.commit(offset, 1000, 0, 500);
  assert(line.reserve(1000, &offset) && offset == 1000);
  line.commit(offset, 1000, 0, 300);  // due earlier: waits for the first
  DelayedFrame frame;
  uint32_t due = 0;
  assert(line.nextDue(&due) && due == 500);
  assert(!line.takeDue(400, &frame, &skipped));
  skipped = 0;
  assert(line.takeDue(500, &frame, &skipped) && frame.offset == 0 && skipped == 0);
  assert(line.takeDue(500, &frame, &skipped) && frame.offset == 1000 && skipped == 0);
  assert(line.empty() && !line.nextDue(&due));

  // After a stall the panel catches up: frames more than kCatchUpMs late go
  // when a newer one is due, the newest due one is shown.
  line.reset(kDelayRingBytes);
  for (uint32_t n = 0; n < 4; ++n) {
    assert(line.reserve(1000, &offset));
    line.commit(offset, 1000, n * 33, 100);
  }
  skipped = 0;
  assert(line.takeDue(100 + 3 * 33 + kCatchUpMs, &frame, &skipped));
  assert(frame.due_ms == 100 + 3 * 33 && skipped == 3 && line.empty());

  // Even spacing (b508): frames arrive in pairs 1 ms apart every 66 ms (the
  // V2 full screen), the task looks every millisecond. Once the line knows
  // the 33 ms rhythm, frames go out 30-36 ms apart instead of 1 ms and 65 ms
  // (b507), each at most one interval after its sound needs it.
  line.reset(kDelayRingBytes);
  ShownGaps gaps;
  uint32_t arrival[256] = {};
  next_id = 1; skipped = 0; last_shown = 0; shown = 0;
  for (uint32_t t = 0; t < 20000; ++t) {
    if (t % 66 == 0 || t % 66 == 1) {
      assert(line.reserve(1000, &offset));
      ring[offset] = static_cast<uint8_t>(next_id & 0xFF);
      arrival[next_id & 0xFF] = t;
      line.commit(offset, 1000, t, 50);
      ++next_id;
    }
    while (line.takeDue(t, &frame, &skipped)) {
      const uint32_t id = ring[frame.offset];
      assert(id == ((last_shown + 1) & 0xFF));
      last_shown = id;
      ++shown;
      assert(t >= arrival[id] + 50 && t <= arrival[id] + 50 + 34);
      if (t >= 3000) gaps.note(t);
    }
  }
  assert(skipped == 0 && shown >= 600);
  assert(line.intervalMs() >= 31 && line.intervalMs() <= 34);
  assert(gaps.count > 200 && gaps.min_ms >= 28 && gaps.max_ms <= 38);

  // The log's gap range: shortest and longest since the line began.
  ShownGaps range;
  range.note(0); range.note(33); range.note(70);
  assert(range.count == 2 && range.min_ms == 33 && range.max_ms == 37);
  range.startLine();
  range.note(100);
  assert(range.count == 1 && range.min_ms == 30 && range.max_ms == 30);
  range.restart();
  range.note(500);
  assert(range.count == 0);

  // A full ring: no room until the oldest goes; a frame larger than the
  // ring never fits (the camera task then shows it unsynced).
  line.reset(3000);
  assert(line.reserve(1500, &offset)); line.commit(offset, 1500, 0, 10);
  assert(line.reserve(1500, &offset) && offset == 1500); line.commit(offset, 1500, 10, 10);
  assert(!line.reserve(1, &offset));
  line.dropOldest();
  assert(line.reserve(1500, &offset) && offset == 0);
  assert(!line.reserve(4000, &offset));
  line.reset(0);
  assert(!line.reserve(1, &offset));
  std::puts("camera lip sync ok");
  return 0;
}
`;

const out = path.join(root, 'build/tests/camera-lip-sync');
fs.mkdirSync(out, {recursive: true});
const cpp = path.join(out, 'camera_lip_sync.cpp');
const exe = path.join(out, process.platform === 'win32' ? 'camera_lip_sync.exe' : 'camera_lip_sync');
fs.writeFileSync(cpp, program);
const build = spawnSync(compiler, ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', root, cpp, '-o', exe],
                        {encoding: 'utf8'});
assert.equal(build.status, 0, `camera lip sync did not compile:\n${build.stdout}${build.stderr}`);
const run = spawnSync(exe, [], {encoding: 'utf8'});
assert.equal(run.status, 0, `camera lip sync failed:\n${run.stdout}${run.stderr}`);
console.log(run.stdout.trim());
