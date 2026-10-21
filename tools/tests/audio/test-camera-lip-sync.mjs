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
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replaceAll('\r\n', '\n');

const camera = read('src/video/camera_stream.cpp');
const loop = camera.slice(camera.indexOf('const uint32_t frames_before = g_worker_frame_count;'),
                          camera.indexOf('frame_count += g_worker_frame_count - frames_before;'));
assert.ok(loop.length > 0, 'the camera task presents frames here');
assert.match(loop, /const uint32_t picture_delay = camera_sync::pictureDelayMs\(speaker::cameraSyncDelayMs\(\)\);/);
assert.match(loop, /if \(picture_delay == 0 && delay_line\.empty\(\)\) \{\s*presented_ok = present_frame\(full, input, jpeg_bytes\);/,
             'without sound every frame goes out at once, as before');
assert.match(loop, /delay_line\.commit\(offset, jpeg_bytes, millis\(\) \+ picture_delay\);/);
assert.match(loop, /if \(delay_line\.takeDue\(millis\(\), &frame, &sync_skipped\)\) \{\s*presented_ok = present_frame\(full, delay_memory \+ frame\.offset, frame\.length\);/);
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
    line.commit(offset, length, now + 200);
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

  // The sound's delay shrinks (or ends): frames catch up and skip, they never
  // overtake an older one.
  line.reset(kDelayRingBytes);
  size_t offset = 0;
  assert(line.reserve(1000, &offset) && offset == 0);
  line.commit(offset, 1000, 500);
  assert(line.reserve(1000, &offset) && offset == 1000);
  line.commit(offset, 1000, 300);  // due earlier: waits for the first
  DelayedFrame frame;
  assert(!line.takeDue(400, &frame, &skipped));
  skipped = 0;
  assert(line.takeDue(500, &frame, &skipped) && frame.offset == 1000 && skipped == 1);
  assert(line.empty());

  // A full ring: no room until the oldest goes; a frame larger than the
  // ring never fits (the camera task then shows it unsynced).
  line.reset(3000);
  assert(line.reserve(1500, &offset)); line.commit(offset, 1500, 10);
  assert(line.reserve(1500, &offset) && offset == 1500); line.commit(offset, 1500, 20);
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
