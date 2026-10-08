// The screensaver wallpaper: fitting it to the screen took 183 ms for
// 1280x800 on the V2 (b303: one division, one coverage test and one blend per
// pixel), and after every opening LVGL rendered the whole screen a second
// time (about 600 ms) although the composite frame was already on screen.
// The cover loop now copies whole rows and rounds only the corner pixels,
// pixel for pixel the same as before; a presented composite frame drops the
// invalidated areas it already shows.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {compileAndRun} from '../../lib/cpp-host.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n/g, '\n');
const source = read('src/ui/screensaver/image_screensaver.cpp');
const fn = name => cppFunctionDefinitions(source).find(f => f.name === name).source;

// make_cover_dsc uses the shared loop with a column map in internal RAM.
const cover = fn('make_cover_dsc');
assert.match(cover, /wallpaper_cover::crop_for\(src_w, src_h, image_w, image_h, focus_x, focus_y,\s*zoom, crop\)/);
assert.match(cover, /heap_caps_malloc\(static_cast<size_t>\(image_w\) \* sizeof\(uint16_t\),\s*MALLOC_CAP_INTERNAL \| MALLOC_CAP_8BIT\)/);
assert.match(cover, /wallpaper_cover::cover_pixels\([\s\S]*?corner_radius, column_map, true\);/,
  'the picture is written as native RGB565 (b305)');
assert.match(cover, /dsc->header\.cf = LV_COLOR_FORMAT_RGB565;/);
assert.match(cover, /heap_caps_free\(column_map\);/);
assert.doesNotMatch(cover, /rounded_pixel_coverage\(x, y/, 'no coverage test per pixel any more');
// The S3 direct decoder keeps using the same helpers.
assert.match(source, /using wallpaper_cover::blend_swapped_rgb565_with_black;\nusing wallpaper_cover::rounded_pixel_coverage;/);

// The composite takes the wallpaper by one copy (composite_over_image.h,
// test-screensaver-composite-lvgl.mjs) and falls back to the snapshot; a
// presented frame drops the invalidated areas it shows.
const present = fn('present_composited_screensaver_frame');
assert.match(present, /if \(!composite_over_wallpaper\(st, display, top_layer\) &&\s*lv_snapshot_take_to_draw_buf\(top_layer, LV_COLOR_FORMAT_RGB565,/);
assert.match(present, /if \(preview_ok\) \{[\s\S]*?lv_inv_area\(display, nullptr\);\s*\}\s*return preview_ok;/);
assert.doesNotMatch(source, /log_snapshot_split/, 'the b304 split diagnostics are gone');

// The new loop against the former per-pixel loop, pixel for pixel: unscaled
// (1280x854 -> 1280x800, the V2 wallpaper), zoomed, a narrow source, focus
// at both ends, radius 0 and a radius larger than the corner.
const out = compileAndRun({
  label: 'Wallpaper cover loop',
  harness: `
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "src/ui/screensaver/wallpaper_cover.h"
using namespace wallpaper_cover;

// The loop as it was up to b303 (image_screensaver.cpp make_cover_dsc).
static bool reference(const uint16_t* src, uint16_t src_w, uint16_t src_h, uint16_t image_w,
                      uint16_t image_h, uint16_t focus_x, uint16_t focus_y, uint16_t zoom,
                      uint16_t radius, std::vector<uint16_t>& out) {
  uint32_t crop_w = src_w;
  uint32_t crop_h = static_cast<uint32_t>((static_cast<uint64_t>(crop_w) * image_h) / image_w);
  if (crop_h > src_h || crop_h == 0) {
    crop_h = src_h;
    crop_w = static_cast<uint32_t>((static_cast<uint64_t>(crop_h) * image_w) / image_h);
    if (crop_w > src_w) crop_w = src_w;
  }
  if (crop_w == 0 || crop_h == 0) return false;
  if (zoom < 1000) zoom = 1000;
  if (zoom > 3000) zoom = 3000;
  crop_w = (crop_w * 1000U) / zoom;
  crop_h = (crop_h * 1000U) / zoom;
  if (crop_w == 0) crop_w = 1;
  if (crop_h == 0) crop_h = 1;
  if (crop_w > src_w) crop_w = src_w;
  if (crop_h > src_h) crop_h = src_h;
  if (focus_x > 1000) focus_x = 1000;
  if (focus_y > 1000) focus_y = 1000;
  const uint32_t x0 = ((src_w - crop_w) * focus_x) / 1000U;
  const uint32_t y0 = ((src_h - crop_h) * focus_y) / 1000U;
  out.assign(static_cast<size_t>(image_w) * image_h, 0);
  for (uint16_t y = 0; y < image_h; ++y) {
    const uint32_t sy = y0 + (static_cast<uint32_t>(y) * crop_h) / image_h;
    const uint16_t* src_row = src + static_cast<size_t>(sy) * src_w;
    uint16_t* dst_row = out.data() + static_cast<size_t>(y) * image_w;
    for (uint16_t x = 0; x < image_w; ++x) {
      const uint32_t sx = x0 + (static_cast<uint32_t>(x) * crop_w) / image_w;
      dst_row[x] = blend_swapped_rgb565_with_black(
          src_row[sx], rounded_pixel_coverage(x, y, image_w, image_h, radius));
    }
  }
  return true;
}

static int check(const char* name, uint16_t src_w, uint16_t src_h, uint16_t image_w, uint16_t image_h,
                 uint16_t focus_x, uint16_t focus_y, uint16_t zoom, uint16_t radius, bool map,
                 bool native = false) {
  std::vector<uint16_t> src(static_cast<size_t>(src_w) * src_h);
  uint32_t seed = 12345u + src_w * 7u + src_h;
  for (auto& px : src) { seed = seed * 1103515245u + 12345u; px = static_cast<uint16_t>(seed >> 16); }
  std::vector<uint16_t> expected;
  const bool ok = reference(src.data(), src_w, src_h, image_w, image_h, focus_x, focus_y, zoom, radius, expected);
  Crop crop;
  const bool ok2 = crop_for(src_w, src_h, image_w, image_h, focus_x, focus_y, zoom, crop);
  if (ok != ok2) { std::printf("%s: crop result differs\\n", name); return 1; }
  if (!ok) { std::printf("%s rejected\\n", name); return 0; }
  std::vector<uint16_t> got(static_cast<size_t>(image_w) * image_h, 0xBEEF);
  std::vector<uint16_t> columns(image_w);
  cover_pixels(src.data(), src_w, crop, got.data(), image_w, image_w, image_h, radius,
               map ? columns.data() : nullptr, native);
  for (size_t i = 0; i < got.size(); ++i) {
    // Native output: the same pixels with their two bytes swapped.
    const uint16_t want = native ? static_cast<uint16_t>((expected[i] >> 8) | (expected[i] << 8)) : expected[i];
    if (got[i] != want) {
      std::printf("%s: pixel %zu,%zu differs\\n", name, i % image_w, i / image_w);
      return 1;
    }
  }
  std::printf("%s same\\n", name);
  return 0;
}

int main() {
  int bad = 0;
  bad |= check("v2-wallpaper", 1280, 854, 1280, 800, 500, 500, 1000, 48, true);
  bad |= check("v2-no-map", 1280, 854, 1280, 800, 0, 1000, 1000, 48, false);
  bad |= check("zoomed", 1280, 854, 1280, 800, 300, 700, 1750, 48, true);
  bad |= check("narrow-source", 600, 900, 1280, 800, 1000, 0, 1000, 30, true);
  bad |= check("square-radius0", 720, 720, 720, 720, 500, 500, 1000, 0, true);
  bad |= check("big-radius", 300, 200, 64, 40, 500, 500, 1000, 25, true);
  bad |= check("tiny", 3, 2, 480, 480, 500, 500, 3000, 34, true);
  bad |= check("v2-native", 1280, 854, 1280, 800, 500, 500, 1000, 48, true, true);
  bad |= check("zoomed-native", 1280, 854, 1280, 800, 300, 700, 1750, 48, true, true);
  return bad;
}
`,
});
if (out !== null) {
  for (const name of ['v2-wallpaper', 'v2-no-map', 'zoomed', 'narrow-source', 'square-radius0', 'big-radius', 'tiny',
    'v2-native', 'zoomed-native']) {
    assert.match(out, new RegExp(`${name} same`), `${name}: the new loop writes the same pixels`);
  }
}
console.log('Screensaver cover: same pixels from whole rows and corner fixes; no second full redraw after the composite');
