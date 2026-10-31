// The screensaver clock keeps its finished pixels (clock_pixel_cache.h):
// closing a popup over the screensaver redrew the clock in ~46 of ~128 ms
// (user 2026-10-31, b640 CloseDraw). Real LVGL, RGB565 bands over a picture:
// with the pixels under the line unchanged a redraw copies the kept result
// and draws none of the line's children, in whole and narrow bands; a new
// picture under it or a new text draws it again; every frame equals the same
// scene drawn without the cache, pixel for pixel.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';

import {readRepoFile} from '../../lib/admin-source.mjs';
import {lvglHost} from '../../lib/lvgl-host.mjs';

const root = path.resolve(import.meta.dirname, '../../..');
const renderer = readRepoFile('src/types/clock/renderer.cpp').replace(/\r\n/g, '\n');

// ---- Wiring -------------------------------------------------------------------
assert.match(renderer, /if \(shadow_out && shadow_out->painter\) \{\s*clock_pixel_cache::attach\(line, &shadow_out->pixels, shadow_out->painter, label\);/,
  'a painted shadow line keeps its pixels');
for (const [where, re] of [
  ['a new text', /shadow_dirty = true;\s*clock_pixel_cache::invalidate\(pixels\);/],
  ['a new width', /void set_box_width\(lv_coord_t width\) \{\s*if \(!line \|\| width < 1\) return;\s*clock_pixel_cache::invalidate\(pixels\);/],
  ['a new mask', /clock_shadow_paint::place\(painter, paint, mask, copies, ext\);\s*clock_pixel_cache::invalidate\(pixels\);/],
  ['the release', /clock_shadow_paint::release\(paint\);\s*clock_pixel_cache::release\(pixels\);/],
]) assert.match(renderer, re, `${where} forgets the kept pixels`);

// ---- Real LVGL ------------------------------------------------------------------
const host = await lvglHost(root);
if (!host) {
  console.log('SKIP: the clock pixels need LVGL and a host compiler (source checks passed)');
  process.exit(0);
}
const out = path.join(root, 'build/tests/clock-pixel-cache');
fs.mkdirSync(out, {recursive: true});
const source = path.join(out, 'test.cpp');
fs.writeFileSync(source, String.raw`
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <lvgl.h>
#include <lvgl_private.h>
#include "src/types/clock/clock_pixel_cache.h"
constexpr int W = 320, H = 200;
static uint16_t frame[W * H];
static int label_draws = 0;
static void flush(lv_display_t* d, const lv_area_t* a, uint8_t* map) {
  const int32_t w = lv_area_get_width(a);
  for (int32_t y = a->y1; y <= a->y2; ++y) std::memcpy(&frame[y * W + a->x1], map + (y - a->y1) * w * 2, w * 2);
  lv_display_flush_ready(d);
}
static lv_image_dsc_t picture(std::vector<uint16_t>& px, int seed) {
  px.resize(W * H);
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x)
      px[y * W + x] = static_cast<uint16_t>((((x + seed) * 31 / W) << 11) | (((x + 2 * y + seed) & 63) << 5) | ((y * 31 / H) ^ (x & 7)));
  lv_image_dsc_t d{};
  d.header.magic = LV_IMAGE_HEADER_MAGIC;
  d.header.cf = LV_COLOR_FORMAT_RGB565;
  d.header.w = W;
  d.header.h = H;
  d.header.stride = W * 2;
  d.data_size = W * H * 2;
  d.data = reinterpret_cast<const uint8_t*>(px.data());
  return d;
}
static clock_pixel_cache::Cache cache;
static lv_obj_t *line, *shadow, *label;
static void attach() { clock_pixel_cache::attach(line, &cache, shadow, label); }
static void detach() {
  lv_obj_remove_event_cb(line, clock_pixel_cache::on_draw_begin);
  lv_obj_remove_event_cb(line, clock_pixel_cache::on_draw_post_end);
}
// The scene drawn again without the cache: the frame it should be.
static std::vector<uint16_t> reference(lv_display_t* d) {
  std::vector<uint16_t> keep(frame, frame + W * H);
  detach();
  lv_obj_invalidate(lv_screen_active());
  lv_refr_now(d);
  std::vector<uint16_t> ref(frame, frame + W * H);
  std::memcpy(frame, keep.data(), sizeof(frame));
  attach();
  return ref;
}
static int refresh(lv_display_t* d, lv_area_t a) {
  label_draws = 0;
  lv_inv_area(d, &a);
  lv_refr_now(d);
  return label_draws;
}
static int differ(const std::vector<uint16_t>& ref) {
  int n = 0;
  for (int i = 0; i < W * H; ++i) n += frame[i] != ref[i];
  return n;
}
int main() {
  lv_init();
  static uint16_t buffer[W * 24];
  lv_display_t* d = lv_display_create(W, H);
  lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(d, buffer, nullptr, sizeof(buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(d, flush);
  std::vector<uint16_t> pa, pb;
  lv_image_dsc_t a = picture(pa, 0), b = picture(pb, 77);
  lv_obj_t* wallpaper = lv_image_create(lv_screen_active());
  lv_image_set_src(wallpaper, &a);
  // The line: a faint dark copy behind white text, both mixing into the
  // pixels under them, the copy reaching past the line.
  line = lv_obj_create(lv_screen_active());
  lv_obj_remove_style_all(line);
  lv_obj_set_pos(line, 40, 60);
  lv_obj_set_size(line, 200, 50);
  lv_obj_add_flag(line, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  shadow = lv_obj_create(line);
  lv_obj_remove_style_all(shadow);
  lv_obj_set_pos(shadow, 4, 4);
  lv_obj_set_size(shadow, 200, 50);
  lv_obj_set_style_bg_color(shadow, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(shadow, 90, 0);
  label = lv_label_create(line);
  lv_obj_set_style_text_color(label, lv_color_white(), 0);
  lv_label_set_text(label, "12:34 Uhr");
  lv_obj_add_event_cb(label, [](lv_event_t*) { ++label_draws; }, LV_EVENT_DRAW_MAIN, nullptr);
  attach();
  lv_refr_now(d);
  // The first frame keeps the pixels.
  std::printf("first %d\n", differ(reference(d)));
  // A popup closing over the line: its band again, then a narrow one.
  int draws = refresh(d, {20, 40, 300, 140});
  std::printf("again %d %d\n", draws, differ(reference(d)));
  draws = refresh(d, {100, 70, 160, 100});
  std::printf("narrow %d %d\n", draws, differ(reference(d)));
  // A new picture under it draws it again, then keeps the new pixels.
  lv_image_set_src(wallpaper, &b);
  draws = refresh(d, {0, 0, W - 1, H - 1});
  std::printf("picture %d %d\n", draws, differ(reference(d)));
  draws = refresh(d, {20, 40, 300, 140});
  std::printf("kept %d %d\n", draws, differ(reference(d)));
  // A new text draws it again.
  lv_label_set_text(label, "12:35 Uhr");
  clock_pixel_cache::invalidate(cache);
  draws = refresh(d, {20, 40, 300, 140});
  std::printf("text %d %d\n", draws, differ(reference(d)));
  // Moved: the old pixels are not used at the new place.
  lv_obj_set_pos(line, 44, 62);
  lv_obj_update_layout(line);
  draws = refresh(d, {0, 0, W - 1, H - 1});
  std::printf("moved %d %d\n", draws, differ(reference(d)));
  clock_pixel_cache::release(cache);
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
  return [key, rest.map(Number)];
}));
const why = `\n${run.stdout}`;
assert.deepEqual(rows.first, [0], `the first frame is the scene's${why}`);
assert.deepEqual(rows.again, [0, 0], `unchanged under it: copied, not drawn, same pixels${why}`);
assert.deepEqual(rows.narrow, [0, 0], `a narrow band is copied too${why}`);
assert.ok(rows.picture[0] > 0 && rows.picture[1] === 0, `a new picture draws it again, the right pixels${why}`);
assert.deepEqual(rows.kept, [0, 0], `then the new result is kept${why}`);
assert.ok(rows.text[0] > 0 && rows.text[1] === 0, `a new text draws it again${why}`);
assert.ok(rows.moved[0] > 0 && rows.moved[1] === 0, `a moved line draws again at its new place${why}`);
console.log('Clock pixel cache: unchanged under the line it copies its kept pixels (whole and narrow bands), anything new draws it again; always the scene\'s pixels');
