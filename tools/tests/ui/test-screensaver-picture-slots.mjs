// The screensaver's picture lives in two fixed slots in the screen's size
// (user 2026-11-02, V2 b647): the shown picture and the next one take turns,
// the composite frame uses the spare slot between two pictures, and only
// the shown picture's slot stays while the screensaver is closed. A fresh
// 2 MB block per slide with the old one freed left gaps that smaller blocks
// (new media covers) settled in, until no 2 MB block was left: the slides
// stopped ("PSRAM budget too small for 2048000 bytes", largest free block
// 1983 KB) and screenshots failed. A 16:9 picture on the 16:10 V2 is cut
// into its cover in its slot, rows from the bottom up, pixel for pixel the
// copy's result.
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';
import {compileAndRun} from '../../lib/cpp-host.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const out = compileAndRun({
  label: 'Wallpaper cover stretched in place',
  harness: `
#include <cstdio>
#include <vector>
#include "src/ui/screensaver/wallpaper_cover.h"
using namespace wallpaper_cover;

// The source at the start of a target-sized buffer (stride src_w, as the
// decoder leaves it), cut in place; the same as the copy into a new buffer.
static int check(uint16_t src_w, uint16_t src_h, uint16_t w, uint16_t h, uint16_t fx, uint16_t fy,
                 uint16_t radius, bool to_native) {
  Crop crop;
  if (!crop_for(src_w, src_h, w, h, fx, fy, 1000, crop)) return -1;
  if (!cover_in_place_ok(crop, src_w, w, h)) return 0;
  std::vector<uint16_t> src(static_cast<size_t>(src_w) * src_h);
  for (size_t i = 0; i < src.size(); ++i) src[i] = static_cast<uint16_t>(i * 2654435761u >> 9);
  std::vector<uint16_t> copy(static_cast<size_t>(w) * h);
  std::vector<uint16_t> map(w), map2(w), row(src_w);
  cover_pixels(src.data(), src_w, crop, copy.data(), w, w, h, radius, map.data(), to_native);
  std::vector<uint16_t> slot(static_cast<size_t>(w) * h, 0xdead);
  std::copy(src.begin(), src.end(), slot.begin());
  cover_in_place(slot.data(), src_w, crop, w, h, radius, map2.data(), row.data(), to_native);
  return slot == copy ? 1 : -2;
}

int main() {
  int same = 0;
  // 16:9 on 16:10 at every focus, also a narrower picture, odd sizes, the
  // same aspect, with and without corners and byte order.
  const uint16_t cases[][4] = {{1280, 720, 1280, 800}, {1920, 1080, 1920, 1200}, {1000, 625, 1280, 800},
                               {37, 20, 40, 30}, {640, 400, 1280, 800}, {480, 270, 480, 480}};
  for (const auto& c : cases)
    for (uint16_t fx : {0, 500, 1000})
      for (uint16_t radius : {0, 13, 36})
        for (int native = 0; native < 2; ++native) {
          const int r = check(c[0], c[1], c[2], c[3], fx, 500, radius, native != 0);
          if (r < 0) { std::printf("differs %ux%u fx%u r%u n%d (%d)\\n", c[0], c[1], fx, radius, native, r); return 1; }
          same += r;
        }
  // Zoomed, cut from the middle of a taller picture or wider rows than the
  // screen's: the copy stays (a source row could be overwritten first).
  Crop crop;
  crop_for(1280, 720, 1280, 800, 500, 500, 1500, crop);
  if (cover_in_place_ok(crop, 1280, 1280, 800)) { std::printf("zoom\\n"); return 1; }
  crop_for(1280, 1024, 1280, 800, 500, 500, 1000, crop);
  if (cover_in_place_ok(crop, 1280, 1280, 800)) { std::printf("taller\\n"); return 1; }
  crop_for(1600, 900, 1280, 800, 500, 500, 1000, crop);
  if (cover_in_place_ok(crop, 1600, 1280, 800)) { std::printf("wider\\n"); return 1; }
  std::printf("ok %d\\n", same);
  return 0;
}
`,
});
if (out !== null) assert.equal(out.trim(), 'ok 108', out);

const source = readRepoFile('src/ui/screensaver/image_screensaver.cpp').replace(/\r\n/g, '\n');
const fn = name => cppFunctionDefinitions(source).find(f => f.name === name)?.source ?? '';

// Two slots; the next picture goes into the one the shown picture does not use.
assert.match(source, /PictureSlot g_slots\[2\];/);
const spare = fn('spare_slot');
assert.match(spare, /const void\* shown = g_cache_dsc \? g_cache_dsc->data : nullptr;\n  PictureSlot\* slot = g_slots\[0\]\.data && g_slots\[0\]\.data == shown \? &g_slots\[1\] : &g_slots\[0\];\n  if \(slot->data && slot->capacity >= bytes\) return slot;/,
  'a slot once fetched is kept');
assert.match(fn('alloc_picture_slot'), /jpeg_alloc_decoder_mem\(bytes, &mem_cfg, &allocated\)/, 'the hardware decoder writes into it');
assert.match(fn('picture_slot_bytes'), /align_up\(w, 16\)\) \*\s*jpeg_padded_output::align_up\(h, 16\)/, 'whole 16x16 blocks fit');

const cached = fn('get_or_decode_cached');
assert.match(cached, /PictureSlot\* slot = spare_slot\(picture_slot_bytes\(w, h\)\);\n  if \(!slot\) return nullptr;/);
assert.match(cached, /corner_radius, millis\(\), 0, \*slot\)/);
assert.match(cached, /wallpaper\.zoom, corner_radius, \*slot\);/);
assert.match(cached, /if \(!g_state\) release_spare_slot\(\);\n  return dsc;/, 'decoded ahead while closed, the old slot goes');

// No picture-sized block per slide any more.
assert.doesNotMatch(fn('make_cover_dsc'), /alloc_prefer_psram|alloc_aligned_prefer_psram|free\(out\)/, 'the cover is written into the slot');
assert.match(fn('make_cover_dsc'), /uint16_t\* out\) \{/);
const composite = fn('ensure_composite_draw_buf');
assert.match(composite, /PictureSlot\* slot = spare_slot\(picture_slot_bytes\(width, height\)\);/, 'the composite frame uses the spare slot');
assert.doesNotMatch(composite, /heap_caps_aligned_alloc|free\(/);
assert.doesNotMatch(source, /composite_storage/, 'no block of its own for the composite frame');
const hw = fn('hw_decode_jpeg');
assert.match(hw, /const bool own = !into \|\| requested_bytes > into_bytes;/);
assert.match(hw, /auto drop = \[&\]\(\) \{\n    if \(own\) free\(decoded\);\n  \};/);
assert.equal((hw.match(/free\(decoded\)/g) || []).length, 1, 'the caller\'s slot is never freed');
assert.match(fn('s3_decode_jpeg_direct_cover'), /ctx\.output = output;/, 'the S3 writes its cover into the slot');
assert.doesNotMatch(fn('s3_decode_jpeg_direct_cover'), /alloc_aligned_prefer_psram|free\(ctx\.output\)/);

// A retired picture keeps its slot's pixels for the next one; closing frees
// the spare slot only.
assert.match(fn('free_screensaver_dsc'), /if \(dsc->data && !in_picture_slot\(dsc->data\)\) \{/);
assert.match(fn('release_spare_slot'), /if \(slot\.data && slot\.data != shown\) \{/);
assert.match(fn('hide_image_screensaver'), /if \(st->image\) lv_image_set_src\(st->image, nullptr\);\n  \/\/ [^\n]*\n  release_spare_slot\(\);/);
assert.match(fn('on_global_overlay_delete'), /if \(!g_state\) release_spare_slot\(\);/);

console.log('Screensaver picture slots: two fixed slots, composite in the spare, 16:9 stretched in place like the copy');
