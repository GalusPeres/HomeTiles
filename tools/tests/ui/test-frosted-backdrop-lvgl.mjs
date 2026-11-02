// Frosted glass behind see-through screensaver tiles (user 2026-11-02, test
// builds): the slide blurred once at a quarter of its size with 8 bits per
// channel; after LVGL drew a see-through card's shadow, fill and line, the
// card writes its area as that copy scaled back up, its tint mixed in,
// Bayer-dithered (no RGB565 bands), its corners anti-aliased, its frame line
// at the card's own opacity. Real LVGL on an RGB565 display over a
// high-contrast picture: inside the card the picture is smooth, the corners
// outside the radius and everything beside the card keep the picture, a
// shadow stays outside the card, the line is translucent, an opaque card
// draws no backdrop, and a swapped (big-endian) picture blurs to the same copy.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';

import {readRepoFile} from '../../lib/admin-source.mjs';
import {lvglHost} from '../../lib/lvgl-host.mjs';

const root = path.resolve(import.meta.dirname, '../../..');
const screensaver = readRepoFile('src/ui/screensaver/image_screensaver.cpp').replace(/\r\n/g, '\n');
const header = readRepoFile('src/ui/screensaver/frosted_backdrop.h').replace(/\r\n/g, '\n');

// ---- Wiring: blurred once per slide, drawn by see-through cards, released ----
assert.match(screensaver, /set_image_src_without_invalidation\(st->image, dsc\);\n  lv_obj_remove_flag\(st->image, LV_OBJ_FLAG_HIDDEN\);\n#if defined\(HOMETILES_FROSTED_TILES\)\n  update_frosted_backdrop\(st, dsc\);/,
  'every new slide is blurred before its frame is composed');
assert.match(screensaver, /if \(opacity < LV_OPA_COVER\) \{[\s\S]*?frosted_backdrop::attach\(tile_obj, &g_backdrop\);/,
  'only see-through cards draw the backdrop');
assert.match(screensaver, /free\(st->frosted\.rgb\);\n  st->frosted = frosted_backdrop::Copy\{\};\n#endif\n  delete st;/, 'the copy goes with the screensaver');
assert.match(screensaver, /picture->header\.cf == LV_COLOR_FORMAT_RGB565_SWAPPED/, 'the S3 and hardware-decoded pictures (swapped) blur too');
assert.match(header, /lv_obj_add_event_cb\(card, on_card_draw_end, LV_EVENT_DRAW_MAIN_END/,
  'drawn after LVGL\'s shadow, fill and line');
assert.match(header, /#if \(defined\(HOMETILES_ISSUE38_BETA\) \|\| defined\(HOMETILES_TEST_BETA\)\) && !defined\(HOMETILES_FROSTED_TILES\)/,
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
#include <set>
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
static int green(int x, int y) { return (frame[y * W + x] >> 5) & 0x3f; }
int main() {
  lv_init();
  static uint16_t buffer[W * 24];
  lv_display_t* d = lv_display_create(W, H);
  lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(d, buffer, nullptr, sizeof(buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(d, flush);
  // A hard 8 px checker of green levels (the blur must smooth it) over a slow
  // red ramp (the dithering must spread it over neighbouring steps).
  std::vector<uint16_t> px(W * H), be(W * H);
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) {
      const uint16_t g = (((x / 8) + (y / 8)) & 1) ? 60 : 4;
      const uint16_t v = static_cast<uint16_t>(((x * 31 / (W - 1)) << 11) | (g << 5) | 8);
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
  std::vector<uint8_t> rgb(frosted_backdrop::copy_bytes(sw, sh)), rgb_be(rgb.size()), scratch(rgb.size());
  std::vector<uint32_t> sums(sw * 3);
  frosted_backdrop::Copy copy{rgb.data(), 0, 0}, copy_be{rgb_be.data(), 0, 0};
  frosted_backdrop::shrink_and_blur(px.data(), W, H, W, false, copy, scratch.data(), sums.data());
  frosted_backdrop::shrink_and_blur(be.data(), W, H, W, true, copy_be, scratch.data(), sums.data());
  std::printf("swap %d\n", rgb == rgb_be && copy.w == sw && copy.h == sh);

  lv_obj_t* wallpaper = lv_image_create(lv_screen_active());
  lv_image_set_src(wallpaper, &pic);
  frosted_backdrop::Backdrop backdrop;
  backdrop.copy = copy;
  backdrop.picture = wallpaper;
  lv_obj_t* card = lv_button_create(lv_screen_active());
  lv_obj_remove_style_all(card);
  lv_obj_set_pos(card, 40, 40);
  lv_obj_set_size(card, 160, 96);
  lv_obj_set_style_radius(card, 20, 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_TRANSP, 0);
  frosted_backdrop::attach(card, &backdrop);
  lv_refr_now(d);
  // Inside the card: smooth. Beside it and in the rounded corner: the checker.
  std::printf("inside %.1f outside %.1f\n", spread(100, 70, 16), spread(240, 140, 16));
  const int corner = 40 * W + 40;
  std::printf("corner %d\n", frame[corner] == px[corner]);
  std::printf("beside %d\n", frame[30 * W + 30] == px[30 * W + 30] && frame[150 * W + 250] == px[150 * W + 250]);
  // The slow red ramp is dithered: an 8x8 block holds neighbouring red steps.
  std::set<int> reds;
  for (int y = 64; y < 72; ++y) for (int x = 120; x < 128; ++x) reds.insert(frame[y * W + x] >> 11);
  std::printf("dither %zu\n", reds.size());
  const int plain_green = green(120, 88);
  // A black tint at 50 % halves the frosted pixels.
  lv_obj_set_style_bg_opa(card, LV_OPA_50, 0);
  lv_refr_now(d);
  std::printf("tint %d %d\n", plain_green, green(120, 88));
  // A shadow stays outside: the inside is the same as without it.
  std::vector<uint16_t> no_shadow(frame, frame + W * H);
  lv_obj_set_style_shadow_width(card, 30, 0);
  lv_obj_set_style_shadow_spread(card, 3, 0);
  lv_obj_set_style_shadow_opa(card, LV_OPA_80, 0);
  lv_obj_set_style_shadow_color(card, lv_color_hex(0x000000), 0);
  lv_refr_now(d);
  int inner_changed = 0, outer_changed = 0;
  for (int y = 46; y < 130; ++y) for (int x = 66; x < 174; ++x) inner_changed += frame[y * W + x] != no_shadow[y * W + x];
  for (int x = 40; x < 200; ++x) outer_changed += frame[36 * W + x] != no_shadow[36 * W + x];
  std::printf("shadow %d %d\n", inner_changed, outer_changed);
  lv_obj_set_style_shadow_width(card, 0, 0);
  // The frame line: white at the card's 50 %, not opaque.
  lv_obj_set_style_outline_width(card, 1, 0);
  lv_obj_set_style_outline_pad(card, -1, 0);
  lv_obj_set_style_outline_color(card, lv_color_hex(0xffffff), 0);
  lv_obj_set_style_outline_opa(card, LV_OPA_COVER, 0);
  lv_refr_now(d);
  const uint16_t edge = frame[90 * W + 40];
  const uint16_t inner = frame[90 * W + 42];
  std::printf("line %d %d %d\n", edge != 0xffff, (edge >> 5 & 0x3f) > (inner >> 5 & 0x3f), edge != inner);
  // An opaque card draws its own fill only.
  lv_obj_set_style_outline_width(card, 0, 0);
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
assert.ok(Number(rows.dither[0]) >= 2, `the gradient is dithered over neighbouring steps${why}`);
const [plain, tinted] = rows.tint.map(Number);
assert.ok(Math.abs(tinted - plain / 2) <= 2, `a 50 % black tint halves the frosted pixels${why}`);
assert.equal(Number(rows.shadow[0]), 0, `the shadow stays out of the card${why}`);
assert.ok(Number(rows.shadow[1]) > 0, `and shows around it${why}`);
assert.deepEqual(rows.line, ['1', '1', '1'], `the frame line is translucent at the card's opacity${why}`);
assert.deepEqual(rows.opaque, ['1'], `an opaque card draws no backdrop${why}`);
console.log('Frosted backdrop: blurred, tinted and dithered under see-through cards, shadow outside, translucent line, corners and surroundings untouched, opaque cards plain');
