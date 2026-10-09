// A picture already in the screen's size without zoom (always the Bridge's
// Home Assistant picture) becomes the screensaver's cover where the decoder
// wrote it: corners and byte order in place instead of a second 2 MB buffer
// and a copy (V2: 85 of 131 ms). Pixel for pixel the copy's result.
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';
import {compileAndRun} from '../../lib/cpp-host.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const out = compileAndRun({
  label: 'Wallpaper cover in place',
  harness: `
#include <cstdio>
#include <vector>
#include "src/ui/screensaver/wallpaper_cover.h"
using namespace wallpaper_cover;

int main() {
  const uint16_t sizes[][2] = {{1280, 800}, {480, 480}, {800, 1280}, {37, 21}};
  const uint16_t radii[] = {0, 13, 36, 300};
  int checked = 0;
  for (const auto& size : sizes) {
    const uint16_t w = size[0], h = size[1];
    std::vector<uint16_t> src(static_cast<size_t>(w) * h);
    for (size_t i = 0; i < src.size(); ++i) src[i] = static_cast<uint16_t>(i * 2654435761u >> 7);
    for (uint16_t radius : radii) {
      for (int native = 0; native < 2; ++native) {
        Crop crop;
        if (!crop_for(w, h, w, h, 137, 911, 1000, crop) || !is_whole_source(crop, w, h, w, h)) {
          std::printf("not whole %ux%u\\n", w, h);
          return 1;
        }
        std::vector<uint16_t> copy(src.size());
        cover_pixels(src.data(), w, crop, copy.data(), w, w, h, radius, nullptr, native != 0);
        std::vector<uint16_t> in_place = src;
        finish_in_place(in_place.data(), w, h, radius, native != 0);
        if (copy != in_place) {
          std::printf("differs %ux%u r%u native%d\\n", w, h, radius, native);
          return 1;
        }
        ++checked;
      }
    }
  }
  // Zoom, another size or a different crop stay with the copy.
  Crop crop;
  crop_for(1280, 800, 1280, 800, 500, 500, 1500, crop);
  if (is_whole_source(crop, 1280, 800, 1280, 800)) { std::printf("zoom\\n"); return 1; }
  crop_for(1280, 854, 1280, 800, 500, 500, 1000, crop);
  if (is_whole_source(crop, 1280, 854, 1280, 800)) { std::printf("size\\n"); return 1; }
  std::printf("ok %d\\n", checked);
  return 0;
}
`,
});
if (out !== null) assert.equal(out.trim(), 'ok 32');

// The screensaver takes the in-place path first and copies only otherwise;
// the in-place buffer must suit the composite (aligned like its own).
const source = readRepoFile('src/ui/screensaver/image_screensaver.cpp');
const fn = name => cppFunctionDefinitions(source).find(f => f.name === name)?.source ?? '';
const finish = fn('finish_cover_in_place');
assert.match(finish, /reinterpret_cast<uintptr_t>\(pixels\) % kPpaBufferAlignment != 0/);
assert.match(finish, /wallpaper_cover::is_whole_source\(crop, w, h, target_w, target_h\)/);
assert.match(finish, /wallpaper_cover::finish_in_place\(pixels, w, h, corner_radius, true\);/);
assert.match(finish, /dsc->data = reinterpret_cast<const uint8_t\*>\(pixels\);/);
const decode = fn('decode_jpeg_to_size');
assert.match(decode, /lv_image_dsc_t\* dsc = finish_cover_in_place\(pixels, w, h, target_w, target_h,\s*focus_x, focus_y, zoom, corner_radius\);[\s\S]*?if \(dsc\) \{\s*pixels = nullptr;[\s\S]*?\} else \{[\s\S]*?dsc = make_cover_dsc\(/,
  'in place first, the copy only otherwise; the buffer then belongs to the picture');
assert.match(decode, /cover %u %s\)\\n"/, 'the log names the way');

console.log('Screensaver cover in place: same pixels as the copy, no second buffer');
