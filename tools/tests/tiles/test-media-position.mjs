// The media position (user 2026-10-22, V2 b522): every volume change made
// the time bar jump back to the last play or pause and left it out of step
// with Home Assistant. Home Assistant's media_position is the position at
// media_position_updated_at, and a volume change repeats both: a state that
// repeats the last report keeps the running position; a new report counts
// the time since it was taken (media_position.h), the Bridge sends that time
// as UTC ISO 8601.
import assert from 'node:assert/strict';
import {compileAndRun} from '../../lib/cpp-host.mjs';
import {readRepoFile} from '../../lib/admin-source.mjs';

const output = compileAndRun({
  label: 'Media position',
  harness: String.raw`
#include <cstdio>
#include "src/types/media/media_position.h"
using namespace media_progress;
static void parse(const char* text) {
  double value = 0.0;
  const bool ok = parse_utc(text, value);
  std::printf("parse %s=%s %.3f\n", text, ok ? "ok" : "no", ok ? value : 0.0);
}
int main() {
  // Home Assistant through the Bridge (dt_util.as_utc(...).isoformat()).
  parse("2026-10-22T15:04:05.123456+00:00");
  parse("2026-10-22T15:04:05+00:00");
  parse("2026-10-22T17:04:05.5+02:00");
  parse("2026-10-22T15:04:05Z");
  parse("2026-10-22 15:04:05");
  parse("1970-01-01T00:00:00+00:00");
  parse("2024-02-29T12:00:00+00:00");
  parse("");
  parse("2026-10-22");
  parse("2026-13-22T15:04:05+00:00");
  parse("2026-10-22T15:04:05+0000");
  parse("2026-10-22T15:04:05+00:00 tail");
  const double taken = 1792681445.0;  // 2026-10-22T15:04:05Z
  // Playing: the reported position plus the time since it was taken.
  std::printf("playing=%.1f\n", now_position(53.0f, 200.0f, true, taken + 12.5, true, taken));
  // Paused: the reported position.
  std::printf("paused=%.1f\n", now_position(53.0f, 200.0f, false, taken + 12.5, true, taken));
  // No clock yet, no time, a time in the future: the reported position.
  std::printf("noclock=%.1f\n", now_position(53.0f, 200.0f, true, 1000.0, true, taken));
  std::printf("notime=%.1f\n", now_position(53.0f, 200.0f, true, taken + 12.5, false, 0.0));
  std::printf("future=%.1f\n", now_position(53.0f, 200.0f, true, taken - 3.0, true, taken));
  // Never past the end, never below zero.
  std::printf("end=%.1f\n", now_position(190.0f, 200.0f, true, taken + 30.0, true, taken));
  std::printf("negative=%.1f\n", now_position(-4.0f, 200.0f, false, taken, true, taken));
  return 0;
}
`,
});

if (output !== null) {
  const lines = output.trim().split('\n');
  assert.deepEqual(lines, [
    'parse 2026-10-22T15:04:05.123456+00:00=ok 1792681445.123',
    'parse 2026-10-22T15:04:05+00:00=ok 1792681445.000',
    'parse 2026-10-22T17:04:05.5+02:00=ok 1792681445.500',
    'parse 2026-10-22T15:04:05Z=ok 1792681445.000',
    'parse 2026-10-22 15:04:05=ok 1792681445.000',
    'parse 1970-01-01T00:00:00+00:00=ok 0.000',
    'parse 2024-02-29T12:00:00+00:00=ok 1709208000.000',
    'parse =no 0.000',
    'parse 2026-10-22=no 0.000',
    'parse 2026-13-22T15:04:05+00:00=no 0.000',
    'parse 2026-10-22T15:04:05+0000=no 0.000',
    'parse 2026-10-22T15:04:05+00:00 tail=no 0.000',
    'playing=65.5',
    'paused=53.0',
    'noclock=53.0',
    'notime=53.0',
    'future=53.0',
    'end=200.0',
    'negative=0.0',
  ]);
}

// The tile keeps the running position while a state repeats the last
// report (same position, same media_position_updated_at, still playing or
// still paused); only a new report sets it and its receipt time again.
const renderer = readRepoFile('src/tiles/runtime/tile_renderer.cpp').replace(/\r\n?/g, '\n');
assert.match(renderer, /extract_json_string_field_cstr\(payload_start, "media_position_updated_at", position_updated_at\);/);
assert.match(renderer, /if \(!media_progress::parse_utc\(position_updated_at\.c_str\(\), position_updated\)\) position_updated = -1\.0;/);
assert.match(renderer, /const bool same_report = had_position && widgets\.has_media_position &&\s*media_position == widgets\.media_position_reported &&\s*position_updated == widgets\.media_position_updated &&\s*playing == widgets\.media_position_playing;/);
assert.match(renderer, /\} else if \(!same_report\) \{[\s\S]*?widgets\.media_position =\s*media_progress::now_position\(media_position, media_duration, playing, now\.tv_sec \+ now\.tv_usec \/ 1e6,\s*position_updated >= 0\.0, position_updated\);\s*widgets\.media_position_received_ms = millis\(\);/);
assert.equal((renderer.match(/widgets\.media_position_received_ms = millis\(\);/g) || []).length, 1,
  'only a new report restarts the running position');
// The page caches copy the tile widgets with memcpy, allocate them with
// calloc and clear them in place: b523 kept the update time as a String in
// MediaTileWidgets, and clearing the 85 KB TileWidgetCache built a temporary
// on the 24 KB buildUI stack (V2 boot loop, "Stack protection fault"). The
// time is a number, and both structs must stay trivially copyable.
const widgets = readRepoFile('src/types/media/widgets.h');
assert.match(widgets, /double media_position_updated = -1\.0;/);
assert.ok(!/String media_position/.test(widgets), 'no String in the tile widgets');
assert.match(widgets, /static_assert\(std::is_trivially_copyable<MediaTileWidgets>::value,/);
assert.match(readRepoFile('src/tiles/runtime/tile_renderer.h'), /static_assert\(std::is_trivially_copyable<TileWidgetCache>::value,/);
const guard = compileAndRun({
  label: 'Media tile widgets copyable',
  harness: String.raw`
#include <cstdio>
#include <string>
#include <type_traits>
struct lv_obj_t;
struct lv_image_dsc_t;
// Arduino's String owns its buffer like std::string.
class String : public std::string { using std::string::string; };
${widgets.replace(/\r\n?/g, '\n').replace(/^#(pragma once|include <(Arduino|lvgl)\.h>)$/gm, '')}
int main() { std::printf("ok\n"); return 0; }
`,
});
if (guard !== null) assert.equal(guard.trim(), 'ok');
console.log('Media position: Home Assistant\'s updated time, a repeated report keeps the running position');
