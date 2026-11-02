// Frosted glass behind see-through screensaver tiles (user 2026-11-02, test
// builds): the slide blurred once at a quarter of its size, drawn under each
// see-through card scaled back up, clipped to the card's rounded shape, under
// the card's own translucent fill. Real LVGL on an RGB565 display over a
// high-contrast picture: inside the card the picture is smooth, the corners
// outside the radius and everything beside the card keep the picture's own
// pixels, an opaque card draws no backdrop, and a swapped (big-endian) picture
// blurs to the same copy.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';

import {readRepoFile} from '../../lib/admin-source.mjs';
import {lvglHost} from '../../lib/lvgl-host.mjs';

const root = path.resolve(import.meta.dirname, '../../..');
const screensaver = readRepoFile('src/ui/screensaver/image_screensaver.cpp').replace(/\r\n/g, '\n');

// ---- Wiring: blurred once per slide, drawn by see-through cards, released ----
assert.match(screensaver, /set_image_src_without_invalidation\(st->image, dsc\);\n  lv_obj_remove_flag\(st->image, LV_OBJ_FLAG_HIDDEN\);\n#if defined\(HOMETILES_FROSTED_TILES\)\n  update_frosted_backdrop\(st, dsc\);/,
  'every new slide is blurred before its frame is composed');
assert.match(screensaver, /if \(opacity < LV_OPA_COVER\) \{[\s\S]*?frosted_backdrop::attach\(tile_obj, &g_backdrop\);/,
  'only see-through cards draw the backdrop');
assert.match(screensaver, /free_screensaver_dsc\(st->frosted\);\n#endif\n  delete st;/, 'the copy goes with the screensaver');
assert.match(screensaver, /g_backdrop\.image = st->frosted;\n  \+\+g_backdrop\.generation;/, 'a new slide samples the corners again');
assert.match(screensaver, /picture->header\.cf == LV_COLOR_FORMAT_RGB565_SWAPPED/, 'the S3 and hardware-decoded pictures (swapped) blur too');
assert.match(readRepoFile('src/ui/screensaver/frosted_backdrop.h'),
  /#if \(defined\(HOMETILES_ISSUE38_BETA\) \|\| defined\(HOMETILES_TEST_BETA\)\) && !defined\(HOMETILES_FROSTED_TILES\)/,
  'test builds only for now');

// ---- Real LVGL ------------------------------------------------------------------
const host = await lvglHost(root);
if (!host) {
  console.log('SKIP: the frosted backdrop needs LVGL and a host compiler (source checks passed)');
  process.exit(0);
}
const out = path.join(root, 'build/tests/frosted-backdrop');
fs.mkdirSync(out, {recursive: true});
const source = path.join(out, 'test.cpp');
fs.writeFileSync(source, String.raw`
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <lvgl.h>
#include <lvgl_private.h>
#include "src/ui/screensaver/frosted_backdrop.h"
constexpr int W = 320, H = 200;
static uint16_t frame[W * H];
static void flush(lv_display_t* d, const lv_area_t* a, uint8_t* map) {
  const int32_t w = lv_area_get_width(a);
  for (int32_t y = a->y1; y <= a->y2; ++y) std::memcpy(&frame[y * W + a->x1], map + (y - a->y1) * w * 2, w * 2);
  lv_display_flush_ready(d);
}
static double spread(int x0, int y0, int n) {
  double sum = 0, sum2 = 0;
  for (int y = y0; y < y0 + n; ++y)
    for (int x = x0; x < x0 + n; ++x) {
      const double g = (frame[y * W + x] >> 5) & 0x3f;
      sum += g; sum2 += g * g;
    }
  const double count = n * n;
  return sum2 / count - (sum / count) * (sum / count);
}
int main() {
  lv_init();
  static uint16_t buffer[W * 24];
  lv_display_t* d = lv_display_create(W, H);
  lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(d, buffer, nullptr, sizeof(buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(d, flush);
  // A hard 8 px checker of green levels (the blur must smooth it) over red
  // and blue ramps (the blurred copy keeps them, so the seam check has slopes).
  std::vector<uint16_t> px(W * H), be(W * H);
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) {
      const uint16_t green = (((x / 8) + (y / 8)) & 1) ? 60 : 4;
      const uint16_t v = static_cast<uint16_t>(((x * 31 / (W - 1)) << 11) | (green << 5) | (y * 31 / (H - 1)));
      px[y * W + x] = v;
      be[y * W + x] = static_cast<uint16_t>((v << 8) | (v >> 8));
    }
  lv_image_dsc_t pic{};
  pic.header.magic = LV_IMAGE_HEADER_MAGIC;
  pic.header.cf = LV_COLOR_FORMAT_RGB565;
  pic.header.w = W; pic.header.h = H; pic.header.stride = W * 2;
  pic.data_size = W * H * 2;
  pic.data = reinterpret_cast<const uint8_t*>(px.data());
  const int sw = frosted_backdrop::shrunk(W), sh = frosted_backdrop::shrunk(H);
  std::vector<uint16_t> small(sw * sh), small_be(sw * sh), scratch(sw * sh);
  frosted_backdrop::shrink_and_blur(px.data(), W, H, W, false, small.data(), scratch.data());
  frosted_backdrop::shrink_and_blur(be.data(), W, H, W, true, small_be.data(), scratch.data());
  std::printf("swap %d\n", small == small_be);
  lv_image_dsc_t blurred{};
  blurred.header.magic = LV_IMAGE_HEADER_MAGIC;
  blurred.header.cf = LV_COLOR_FORMAT_RGB565;
  blurred.header.w = sw; blurred.header.h = sh; blurred.header.stride = sw * 2;
  blurred.data_size = sw * sh * 2;
  blurred.data = reinterpret_cast<const uint8_t*>(small.data());

  lv_obj_t* wallpaper = lv_image_create(lv_screen_active());
  lv_image_set_src(wallpaper, &pic);
  frosted_backdrop::Backdrop backdrop{&blurred, wallpaper, 1};
  lv_obj_t* card = lv_button_create(lv_screen_active());
  lv_obj_remove_style_all(card);
  lv_obj_set_pos(card, 40, 40);
  lv_obj_set_size(card, 160, 96);
  lv_obj_set_style_radius(card, 20, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_TRANSP, 0);
  frosted_backdrop::attach(card, &backdrop);
  lv_refr_now(d);
  // Inside the card: smooth. Beside it and in the rounded corner: the checker.
  std::printf("inside %.1f outside %.1f\n", spread(100, 70, 16), spread(240, 140, 16));
  const int corner = 40 * W + 40;
  std::printf("corner %d\n", frame[corner] == px[corner]);
  std::printf("beside %d\n", frame[30 * W + 30] == px[30 * W + 30] && frame[150 * W + 250] == px[150 * W + 250]);
  // The corner pieces match the scaled copy: the same card without radius
  // is the reference for every pixel well inside the rounded corners.
  std::vector<uint16_t> rounded(frame, frame + W * H);
  lv_obj_set_style_radius(card, 0, 0);
  lv_refr_now(d);
  std::vector<uint16_t> square(frame, frame + W * H);
  int worst = 0;
  for (int y = 40; y < 136; ++y)
    for (int x = 40; x < 200; ++x) {
      const int cx = x < 60 ? 60 : (x > 179 ? 179 : x), cy = y < 60 ? 60 : (y > 115 ? 115 : y);
      if ((x - cx) * (x - cx) + (y - cy) * (y - cy) > 17 * 17) continue;  // the antialiased rim
      const uint16_t a = rounded[y * W + x], b = square[y * W + x];
      for (int shift : {11, 5, 0}) {
        const int mask = shift == 5 ? 0x3f : 0x1f;
        const int dc = static_cast<int>((a >> shift) & mask) - static_cast<int>((b >> shift) & mask);
        if ((dc < 0 ? -dc : dc) > worst) worst = dc < 0 ? -dc : dc;
      }
    }
  std::printf("seam %d\n", worst);
  lv_obj_set_style_radius(card, 20, 0);
  // An opaque card draws its own fill only.
  lv_obj_set_style_bg_color(card, lv_color_hex(0x203040), 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_refr_now(d);
  const uint16_t fill = lv_color_to_u16(lv_color_hex(0x203040));
  std::printf("opaque %d\n", frame[88 * W + 120] == fill);
  return 0;
}
`);
const binary = path.join(out, 'test' + (process.platform === 'win32' ? '.exe' : ''));
let run = spawnSync(host.cxx, [...host.flags, '-std=c++17', '-Wno-deprecated-declarations', '-I', root, source, host.archive, '-o', binary],
  {encoding: 'utf8'});
assert.equal(run.status, 0, run.stdout + run.stderr);
run = spawnSync(binary, [], {encoding: 'utf8'});
assert.equal(run.status, 0, run.stdout + run.stderr);
const rows = Object.fromEntries(run.stdout.trim().split('\n').map((line) => {
  const [key, ...rest] = line.trim().split(' ');
  return [key, rest];
}));
const why = `\n${run.stdout}`;
assert.deepEqual(rows.swap, ['1'], `a swapped picture blurs to the same copy${why}`);
const inside = Number(rows.inside[0]);
const outside = Number(rows.inside[2]);
assert.ok(outside > 500 && inside < outside / 20, `the card shows the blurred picture, the rest the sharp one${why}`);
assert.deepEqual(rows.corner, ['1'], `the rounded corner keeps the picture${why}`);
assert.deepEqual(rows.beside, ['1'], `nothing beside the card changes${why}`);
assert.deepEqual(rows.opaque, ['1'], `an opaque card draws no backdrop${why}`);
assert.ok(Number(rows.seam[0]) <= 1, `the corner pieces meet the scaled copy without a step${why}`);
console.log('Frosted backdrop: blurred under see-through cards, rounded corners and surroundings untouched, opaque cards plain, swapped pictures alike');
