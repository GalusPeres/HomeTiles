// The screensaver clock's soft shadow: nine faint dark copies of each line,
// each a label of the large compressed clock font, cost about 110 ms of
// every screensaver opening on the V2 (b304); one composed ARGB picture per
// opening cost 220 ms (b305, reverted), one composed A8 picture came out
// visibly lighter on RGB565 panels (b306 trial: LVGL rounds every copy down
// in 5 bits, which makes the shadow darker than its opacities). The text is
// drawn once into an A8 mask; nine A8 images of it (b306) still cost about
// 87 ms per frame on the V2 (b308). One painter object now draws the nine
// copies (src/types/clock/clock_shadow_paint.h): on an RGB565 layer with
// LVGL's own arithmetic, row by row, else with the images' LVGL draws. On
// real LVGL its pixels equal the nine images' and the nine labels' in the
// bands of an RGB565 display, in RGB565 and XRGB8888 snapshots and inside a
// see-through container; the RGB565 cases take the direct path.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {lvglHost} from '../../lib/lvgl-host.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n/g, '\n');

const renderer = read('src/types/clock/renderer.cpp');
const fns = cppFunctionDefinitions(renderer);
const fn = name => fns.find(f => f.name === name).source;
// The line: a hidden mask label and the painter behind the white text; the
// copy labels only without them (full-width lines).
const line = fn('create_clock_line');
assert.match(line, /if \(!config\.fill_parent && shadow_out\) \{[\s\S]*?lv_obj_t\* mask = lv_label_create\(line\);/);
assert.match(line, /lv_obj_add_flag\(mask, LV_OBJ_FLAG_HIDDEN\);/);
assert.match(line, /lv_obj_t\* painter = clock_shadow_paint::create\(line, &shadow_out->paint\);/);
assert.match(line, /if \(!shadow_out \|\| !shadow_out->mask_label\) \{\s*for \(uint8_t i = 0; i < kClockShadowCopies; \+\+i\) \{/);
assert.ok(line.indexOf('clock_shadow_paint::create(line') < line.lastIndexOf('lv_obj_t* label = lv_label_create(line);'),
  'the copies lie behind the white text');
// The painter draws from the line's state in ClockTileData: created there
// before the lines, never copied.
const widget = fn('create_clock_widget');
assert.ok(widget.indexOf('new (std::nothrow) ClockTileData{}') < widget.indexOf('create_clock_line('),
  'the data exists before the lines');
assert.match(widget, /&data->time_shadows\);/);
assert.match(widget, /&data->date_shadows\);/);
assert.doesNotMatch(widget, /data->time_shadows = |data->date_shadows = /);
assert.match(renderer, /ClockShadowSet\(const ClockShadowSet&\) = delete;/);
// The mask: from the cache when the line is unchanged, else drawn once.
const mask = fn('clock_shadow_mask');
assert.match(mask, /entry\.font == font && entry\.width == width && entry\.height == height &&\s*entry\.alignment == alignment && strcmp\(entry\.text, text\) == 0\) \{[\s\S]*?return lv_draw_buf_dup\(entry\.mask\);/);
assert.match(mask, /lv_snapshot_take\(mask_label, LV_COLOR_FORMAT_A8\)/);
assert.match(mask, /slot->ext = lv_obj_get_ext_draw_size\(mask_label\);/);
assert.match(renderer, /clock_shadow_paint::place\(painter, paint, mask, copies, ext\);/);
assert.match(fn('update_clock_labels'), /if \(changed\) \{\s*apply_clock_line_alignment\(data\);[\s\S]*?time_shadows\.refresh_shadow\(\);\s*data->date_shadows\.refresh_shadow\(\);/);
assert.match(renderer, /data->time_shadows\.release_shadow\(\);\s*data->date_shadows\.release_shadow\(\);\s*delete data;/);

const host = await lvglHost(root);
if (!host) {
  console.log('SKIP: the clock shadow test needs LVGL and a host compiler');
  process.exit(0);
}
const out = path.join(root, 'build/tests/clock-shadow-mask');
fs.mkdirSync(out, {recursive: true});
const source = path.join(out, 'test.cpp');
fs.writeFileSync(source, String.raw`
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "src/types/clock/clock_shadow_paint.h"
extern "C" { LV_FONT_DECLARE(ui_font_96); }

// The renderer's copies at the 1280x800 scale.
using clock_shadow_paint::Copy;
static const Copy kCopies[9] = {
    {4, 4, 34}, {2, 4, 14}, {6, 4, 14}, {4, 2, 14}, {4, 6, 14}, {2, 2, 8}, {6, 2, 8}, {2, 6, 8}, {6, 6, 8}};
static const int W = 560, H = 220, BAND = 13;

enum Mode { LABELS, IMAGES, PAINTER };
static clock_shadow_paint::Shadow g_shadow;
static int g_lvgl_draws = 0;
static lv_draw_buf_t* g_mask = nullptr;

static lv_obj_t* label(lv_obj_t* parent, const char* text, lv_point_t size, lv_color_t color, lv_opa_t opa) {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, &ui_font_96, 0);
  lv_obj_set_style_text_color(l, color, 0);
  lv_obj_set_style_text_opa(l, opa, 0);
  lv_obj_set_size(l, size.x, size.y);
  lv_label_set_text(l, text);
  return l;
}

// A clock line as the renderer builds it, its shadow drawn three ways.
static void make_line(lv_obj_t* parent, const char* text, Mode mode) {
  lv_point_t size;
  lv_text_get_size(&size, text, &ui_font_96, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  lv_obj_t* line = lv_obj_create(parent);
  lv_obj_remove_style_all(line);
  lv_obj_set_pos(line, 37, 41);
  lv_obj_set_size(line, size.x, size.y);
  lv_obj_add_flag(line, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  if (mode == LABELS) {
    for (const Copy& c : kCopies) lv_obj_set_pos(label(line, text, size, lv_color_black(), c.opa), c.x, c.y);
  } else {
    lv_obj_t* mask_label = label(line, text, size, lv_color_white(), LV_OPA_COVER);
    lv_obj_add_flag(mask_label, LV_OBJ_FLAG_HIDDEN);
    if (g_mask) lv_draw_buf_destroy(g_mask);
    g_mask = lv_snapshot_take(mask_label, LV_COLOR_FORMAT_A8);
    if (!g_mask) { std::printf("no mask\n"); std::exit(1); }
    const int32_t ext = lv_obj_get_ext_draw_size(mask_label);
    if (mode == IMAGES) {
      for (const Copy& c : kCopies) {
        lv_obj_t* image = lv_image_create(line);
        lv_obj_set_pos(image, c.x - ext, c.y - ext);
        lv_obj_set_style_image_recolor(image, lv_color_black(), 0);
        lv_obj_set_style_image_opa(image, c.opa, 0);
        lv_image_set_src(image, g_mask);
      }
    } else {
      lv_obj_t* painter = clock_shadow_paint::create(line, &g_shadow);
      // Counts the draws handed to LVGL: none on the direct path.
      lv_obj_add_flag(painter, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);
      lv_obj_add_event_cb(painter, [](lv_event_t*) { ++g_lvgl_draws; }, LV_EVENT_DRAW_TASK_ADDED, nullptr);
      clock_shadow_paint::place(painter, g_shadow, g_mask, kCopies, ext);
    }
  }
  label(line, text, size, lv_color_white(), LV_OPA_COVER);
}

static std::vector<uint16_t> g_frame(W * H);
static void flush(lv_display_t* d, const lv_area_t* area, uint8_t* px) {
  const int32_t w = lv_area_get_width(area);
  for (int32_t y = area->y1; y <= area->y2; ++y) memcpy(&g_frame[y * W + area->x1], px + (y - area->y1) * w * 2, w * 2);
  lv_display_flush_ready(d);
}

struct Shot { std::vector<uint16_t> frame; lv_draw_buf_t* rgb565; lv_draw_buf_t* xrgb; int draws[3]; };

static Shot render(lv_obj_t* screen, Mode mode, lv_opa_t container_opa) {
  lv_obj_t* box = lv_obj_create(screen);
  lv_obj_remove_style_all(box);
  lv_obj_set_size(box, W, H);
  lv_obj_add_flag(box, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  if (container_opa < LV_OPA_COVER) lv_obj_set_style_opa(box, container_opa, 0);
  make_line(box, "12:34", mode);
  lv_obj_invalidate(screen);
  Shot shot{};
  g_lvgl_draws = 0;
  lv_refr_now(nullptr);  // in bands of BAND rows
  shot.frame = g_frame;
  shot.draws[0] = g_lvgl_draws;
  g_lvgl_draws = 0;
  shot.rgb565 = lv_snapshot_take(screen, LV_COLOR_FORMAT_RGB565);
  shot.draws[1] = g_lvgl_draws;
  g_lvgl_draws = 0;
  shot.xrgb = lv_snapshot_take(screen, LV_COLOR_FORMAT_XRGB8888);
  shot.draws[2] = g_lvgl_draws;
  lv_obj_delete(box);
  return shot;
}

static int diff565(const uint16_t* a, const uint16_t* b, size_t n, long* changed) {
  int m = 0;
  for (size_t i = 0; i < n; ++i) {
    const int d = std::max({std::abs((a[i] >> 11) - (b[i] >> 11)), std::abs(((a[i] >> 5) & 63) - ((b[i] >> 5) & 63)),
                            std::abs((a[i] & 31) - (b[i] & 31))});
    if (d && changed) ++*changed;
    m = std::max(m, d);
  }
  return m;
}

static int diff_buf(lv_draw_buf_t* a, lv_draw_buf_t* b) {
  int m = 0;
  for (uint32_t y = 0; y < a->header.h; ++y) {
    const uint8_t* ra = a->data + y * a->header.stride;
    const uint8_t* rb = b->data + y * b->header.stride;
    if (a->header.cf == LV_COLOR_FORMAT_RGB565) {
      m = std::max(m, diff565(reinterpret_cast<const uint16_t*>(ra), reinterpret_cast<const uint16_t*>(rb), a->header.w, nullptr));
    } else {
      for (uint32_t x = 0; x < a->header.w * 4; ++x) if ((x & 3) != 3) m = std::max(m, std::abs(ra[x] - rb[x]));
    }
  }
  return m;
}

int main() {
  lv_init();
  lv_display_t* display = lv_display_create(W, H);
  lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
  static uint8_t buffer[W * BAND * 2];
  lv_display_set_buffers(display, buffer, nullptr, sizeof(buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(display, flush);

  // A wallpaper with a different colour in every pixel.
  static std::vector<uint16_t> pixels(W * H);
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x)
      pixels[y * W + x] = static_cast<uint16_t>(((x * 31 / W) << 11) | (((x + 2 * y) & 63) << 5) | ((y * 31 / H) ^ (x & 7)));
  lv_image_dsc_t picture{};
  picture.header.magic = LV_IMAGE_HEADER_MAGIC;
  picture.header.cf = LV_COLOR_FORMAT_RGB565;
  picture.header.w = W;
  picture.header.h = H;
  picture.header.stride = W * 2;
  picture.data_size = W * H * 2;
  picture.data = reinterpret_cast<const uint8_t*>(pixels.data());
  lv_obj_t* screen = lv_screen_active();
  lv_image_set_src(lv_image_create(screen), &picture);

  for (lv_opa_t opa : {static_cast<lv_opa_t>(LV_OPA_COVER), static_cast<lv_opa_t>(180)}) {
    Shot labels = render(screen, LABELS, opa);
    Shot images = render(screen, IMAGES, opa);
    Shot painter = render(screen, PAINTER, opa);
    long shadowed = 0;
    diff565(labels.frame.data(), pixels.data(), pixels.size(), &shadowed);
    std::printf("opa %u shadowed %ld display %d %d rgb565 %d %d xrgb %d %d draws %d %d %d\n", opa, shadowed,
                diff565(images.frame.data(), painter.frame.data(), W * H, nullptr),
                diff565(labels.frame.data(), painter.frame.data(), W * H, nullptr),
                diff_buf(images.rgb565, painter.rgb565), diff_buf(labels.rgb565, painter.rgb565),
                diff_buf(images.xrgb, painter.xrgb), diff_buf(labels.xrgb, painter.xrgb),
                painter.draws[0], painter.draws[1], painter.draws[2]);
  }
  return 0;
}
`);
const binary = path.join(out, 'clock-shadow' + (process.platform === 'win32' ? '.exe' : ''));
let run = spawnSync(host.cxx, [...host.flags, '-std=c++17', '-Wno-deprecated-declarations', source, host.archive, '-o', binary],
  {encoding: 'utf8'});
assert.equal(run.status, 0, run.stdout + run.stderr);
run = spawnSync(binary, [], {encoding: 'utf8'});
assert.equal(run.status, 0, run.stdout + run.stderr);
const lines = [...run.stdout.matchAll(/opa (\d+) shadowed (\d+) display (\d+) (\d+) rgb565 (\d+) (\d+) xrgb (\d+) (\d+) draws (\d+) (\d+) (\d+)/g)];
assert.equal(lines.length, 2, run.stdout);
for (const [, opa, shadowed, ...rest] of lines) {
  const [displayImages, displayLabels, rgbImages, rgbLabels, xrgbImages, xrgbLabels, displayDraws, rgbDraws, xrgbDraws] = rest.map(Number);
  assert.ok(Number(shadowed) > 500, `opa ${opa}: the shadow darkens the wallpaper (${shadowed} px)`);
  assert.deepEqual([displayImages, displayLabels, rgbImages, rgbLabels, xrgbImages, xrgbLabels], [0, 0, 0, 0, 0, 0],
    `opa ${opa}: the painter gives the images' and the labels' pixels\n${run.stdout}`);
  assert.deepEqual([displayDraws, rgbDraws], [0, 0], `opa ${opa}: RGB565 takes the direct path\n${run.stdout}`);
  assert.equal(xrgbDraws, 9, `opa ${opa}: XRGB8888 hands LVGL the nine copies\n${run.stdout}`);
}
console.log('Clock shadow: one painter draws the nine copies with the same pixels (RGB565 bands and snapshot directly, XRGB8888 by LVGL)');
