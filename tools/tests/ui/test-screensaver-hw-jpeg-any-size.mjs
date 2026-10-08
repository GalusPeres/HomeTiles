// Screensaver wallpapers of any size use the P4 JPEG hardware. Before, only
// widths and heights in multiples of 16 did; a 1280x854 wallpaper fell back to
// TJpgDec and held the loop for 727 ms at boot and on every slide (V2 b300).
// The decoder writes whole MCUs (esp_driver_jpeg: width and height rounded up
// to the luma MCU), so the buffer has padded rows and the padding is cut off.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {compileAndRun} from '../../lib/cpp-host.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n/g, '\n');

const decode = cppFunctionDefinitions(read('src/ui/screensaver/image_screensaver.cpp'))
  .find(f => f.name === 'hw_decode_jpeg').source;
assert.doesNotMatch(decode, /& 15U/, 'no size is turned away from the hardware');
assert.match(decode, /info\.sample_method == JPEG_DOWN_SAMPLING_YUV420\) \{\s*mcu_w = 16;\s*mcu_h = 16;/);
assert.match(decode, /info\.sample_method == JPEG_DOWN_SAMPLING_YUV422\) \{\s*mcu_w = 16;\s*\}/);
assert.match(decode, /const uint32_t stride = jpeg_padded_output::align_up\(info\.width, mcu_w\);/);
assert.match(decode, /const uint32_t rows = jpeg_padded_output::align_up\(info\.height, mcu_h\);/);
// The padded size is what is allocated, budgeted and checked.
assert.match(decode, /const uint32_t pixels = stride \* rows;\s*if \(pixels > kMaxDecodePixels\) return nullptr;/);
// A layout other than the assumed one goes to TJpgDec instead of shifting rows.
const check = decode.indexOf('if (decoded_bytes != requested_bytes) {');
const crop = decode.indexOf('jpeg_padded_output::crop_in_place(decoded, stride, info.width, info.height);');
assert.ok(check > 0 && crop > check, 'the written size is checked before the padding is cut off');
assert.ok(crop < decode.indexOf('out_w = static_cast<uint16_t>(info.width);'));

// The crop itself, pixel by pixel, on the host: 1280x854 as 4:2:0 (rows of
// 1280, 864 rows), a 4:4:4 width that is not a multiple of 8 and an aligned
// image that stays untouched.
const out = compileAndRun({
  label: 'JPEG padded output crop',
  harness: `
#include <cstdio>
#include <vector>
#include "src/core/display/jpeg_padded_output.h"
using namespace jpeg_padded_output;
static int check(uint32_t w, uint32_t h, uint32_t mcu_w, uint32_t mcu_h) {
  const uint32_t stride = align_up(w, mcu_w), rows = align_up(h, mcu_h);
  std::vector<uint16_t> buf(static_cast<size_t>(stride) * rows);
  for (uint32_t y = 0; y < rows; ++y)
    for (uint32_t x = 0; x < stride; ++x)
      buf[static_cast<size_t>(y) * stride + x] = (x < w && y < h) ? static_cast<uint16_t>((y * 131u + x * 7u) & 0xFFFF) : 0xDEAD;
  crop_in_place(buf.data(), stride, w, h);
  for (uint32_t y = 0; y < h; ++y)
    for (uint32_t x = 0; x < w; ++x)
      if (buf[static_cast<size_t>(y) * w + x] != static_cast<uint16_t>((y * 131u + x * 7u) & 0xFFFF)) {
        std::printf("bad %ux%u at %u,%u\\n", w, h, x, y);
        return 1;
      }
  std::printf("%ux%u stride %u rows %u ok\\n", w, h, stride, rows);
  return 0;
}
int main() {
  int bad = 0;
  bad |= check(1280, 854, 16, 16);
  bad |= check(1283, 801, 8, 8);
  bad |= check(145, 145, 16, 8);
  bad |= check(1280, 800, 16, 16);
  return bad;
}
`,
});
if (out !== null) {
  assert.match(out, /1280x854 stride 1280 rows 864 ok/);
  assert.match(out, /1283x801 stride 1288 rows 808 ok/);
  assert.match(out, /145x145 stride 160 rows 152 ok/);
  assert.match(out, /1280x800 stride 1280 rows 800 ok/);
}
console.log('Screensaver: any JPEG size on the P4 hardware, padding cut off exactly');
