// LVGL masked every row of a rounded corner on its own (V2 b592 FrameProbe:
// card.fill 9.0 + card.border 12.3 ms of a 114 ms page turn; user 2026-10-28:
// make every tile cheaper). Tile cards and icon discs now draw their rounded
// fill and frame line from corner coverage LVGL computed once
// (src/ui/shared/rounded_corners.cpp). Real LVGL draws the same scene in
// 28-row RGB565 bands with and without it, plus a partial redraw cutting
// through the corners, over 30 opacities of a see-through card (more corner
// sets than are kept, so old ones are replaced): every pixel must match.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {lvglHost} from '../../lib/lvgl-host.mjs';

const root = path.resolve(import.meta.dirname, '../../..');
const read = (p) => fs.readFileSync(path.join(root, p), 'utf8').replace(/\r\n/g, '\n');

const renderer = read('src/tiles/runtime/tile_renderer.cpp');
assert.ok(renderer.includes('rounded_corners::attach_tree(tile_obj);') &&
  renderer.indexOf('rounded_corners::attach_tree(tile_obj);') < renderer.indexOf('tile_frame_probe::watch(tile_obj, index'),
  'every rendered tile card draws from kept corners, attached before the probe so it times them');
assert.ok(renderer.includes('rounded_corners::attach_tree(tile_obj);'), 'and every rounded object inside it: discs, control bars, pills');
const corners = read('src/ui/shared/rounded_corners.cpp');
assert.ok(corners.includes('if (fill->grad.dir != LV_GRAD_DIR_NONE || fill->opa <= LV_OPA_MIN || fill->radius <= 0) return;'),
  'gradients keep LVGL\'s drawing');
assert.ok(corners.includes('if (!base || base->part != LV_PART_MAIN || base->drop_shadow_opa || base->user_data) return;'),
  'only the main part without a drop shadow');
// One task per fill and line (b593 added the parts as tasks: ~900 more per
// page turn cost what the corners saved): the task is marked and drawn by the
// software unit's dispatch, nothing else is added.
assert.ok(corners.includes('g_next_dispatch = unit->dispatch_cb;') && corners.includes('unit->dispatch_cb = corner_dispatch;'),
  'marked tasks are drawn in the software unit dispatch');
assert.doesNotMatch(corners, /lv_draw_image\(|lv_draw_fill\(/, 'no draw tasks are added');
assert.ok(corners.includes('if (border->side != LV_BORDER_SIDE_FULL || border->opa != LV_OPA_COVER'),
  'partial or translucent borders keep LVGL\'s drawing');
assert.ok(corners.includes('if (set.used_frame != g_frame && (!oldest || set.used_frame < oldest->used_frame)) oldest = &set;'),
  'a corner set used in this frame is never replaced');
assert.ok(corners.includes('lv_image_cache_drop(&image);') && corners.includes('lv_image_header_cache_drop(&image);'),
  'a replaced set leaves no stale image in LVGL\'s caches');

const host = await lvglHost(root);
if (!host) {
  console.log('SKIP: the rounded corner pixel run needs LVGL and a host compiler (source checks passed)');
  process.exit(0);
}

const cpp = String.raw`
#include <lvgl.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include "src/ui/shared/rounded_corners.h"

static constexpr int W = 760, H = 420, kBand = 28;
static std::vector<uint16_t> g_screen(W * H);
static std::vector<uint16_t> g_band(W * kBand);

static void flush(lv_display_t* d, const lv_area_t* a, uint8_t* px) {
  const int w = a->x2 - a->x1 + 1;
  const uint16_t* src = reinterpret_cast<const uint16_t*>(px);
  for (int y = a->y1; y <= a->y2; ++y) memcpy(&g_screen[y * W + a->x1], src + (y - a->y1) * w, w * 2);
  lv_display_flush_ready(d);
}

struct Card {
  int x, y, w, h, radius;
  uint32_t color;
  int opa;  // -1: the pass's see-through opacity
  int line;
  uint32_t line_color;
  int disc, disc_opa;
};
static const Card kCards[] = {
    {13, 7, 194, 152, 31, 0x1A1A1A, 255, 1, 0x2E2E2E, 84, 51},
    {220, 7, 299, 152, 31, 0x3A5A8C, 255, 1, 0x505050, 84, 255},
    {13, 171, 194, 68, 31, 0x203040, 255, 1, 0x404040, 56, 40},
    {530, 7, 210, 152, 31, 0x804020, -1, 1, 0x202020, 0, 0},
    {220, 171, 404, 230, 31, 0x123456, 255, 2, 0x8899AA, 100, 90},
    {13, 255, 120, 60, 31, 0x445566, 255, 1, 0x223344, 0, 0},
    {640, 171, 101, 101, LV_RADIUS_CIRCLE, 0xAA3355, 255, 1, 0x111111, 0, 0},
};

static void build(bool kept, int see_through) {
  lv_obj_t* screen = lv_obj_create(nullptr);
  lv_obj_remove_style_all(screen);
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x0B0D10), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  for (const Card& c : kCards) {
    lv_obj_t* card = lv_obj_create(screen);
    lv_obj_remove_style_all(card);
    lv_obj_set_pos(card, c.x, c.y);
    lv_obj_set_size(card, c.w, c.h);
    lv_obj_set_style_radius(card, c.radius, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(c.color), 0);
    lv_obj_set_style_bg_opa(card, c.opa < 0 ? see_through : c.opa, 0);
    // The tile frame line (ui_surface_style.cpp): an outline 1 px inside the edge.
    lv_obj_set_style_outline_width(card, c.line, 0);
    lv_obj_set_style_outline_pad(card, -c.line, 0);
    lv_obj_set_style_outline_color(card, lv_color_hex(c.line_color), 0);
    lv_obj_set_style_outline_opa(card, LV_OPA_COVER, 0);
    lv_obj_t* disc = nullptr;
    if (c.disc) {
      disc = lv_obj_create(card);
      lv_obj_remove_style_all(disc);
      lv_obj_set_pos(disc, 12, 13);
      lv_obj_set_size(disc, c.disc, c.disc);
      lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_bg_color(disc, lv_color_hex(0xF0E0D0), 0);
      lv_obj_set_style_bg_opa(disc, c.disc_opa, 0);
    }
    if (kept) {
      rounded_corners::attach(card);
      if (disc) rounded_corners::attach(disc);
    }
  }
  lv_obj_t* old = lv_screen_active();
  lv_screen_load(screen);
  if (old) lv_obj_delete(old);
}

// The whole screen, then a partial redraw cutting through corners.
static void render(std::vector<uint16_t>& full, std::vector<uint16_t>& part) {
  lv_obj_invalidate(lv_screen_active());
  lv_refr_now(nullptr);
  full = g_screen;
  const lv_area_t cut = {100, 20, 350, 60};
  for (int y = cut.y1; y <= cut.y2; ++y)
    for (int x = cut.x1; x <= cut.x2; ++x) g_screen[y * W + x] = 0xF81F;
  lv_obj_invalidate_area(lv_screen_active(), &cut);
  lv_refr_now(nullptr);
  part = g_screen;
}

int main() {
  lv_init();
  lv_display_t* display = lv_display_create(W, H);
  lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(display, g_band.data(), nullptr, g_band.size() * 2, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(display, flush);
  unsigned diffs = 0, first_x = 0, first_y = 0, first_a = 0, first_b = 0;
  std::vector<uint16_t> plain_full, plain_part, kept_full, kept_part;
  for (int pass = 0; pass < 30; ++pass) {
    const int see_through = 60 + pass * 6;
    build(false, see_through);
    render(plain_full, plain_part);
    build(true, see_through);
    render(kept_full, kept_part);
    for (int i = 0; i < W * H; ++i) {
      const bool full_diff = plain_full[i] != kept_full[i];
      const bool part_diff = plain_part[i] != kept_part[i];
      if (!full_diff && !part_diff) continue;
      if (!diffs) {
        first_x = i % W; first_y = i / W;
        first_a = full_diff ? plain_full[i] : plain_part[i];
        first_b = full_diff ? kept_full[i] : kept_part[i];
      }
      ++diffs;
    }
  }
  const rounded_corners::Stats s = rounded_corners::stats();
  printf("{\"diffs\":%u,\"first\":[%u,%u,%u,%u],\"fills\":%u,\"borders\":%u,\"drawn\":%u,\"built\":%u,\"masks\":%u,\"declined\":%u}\n",
         diffs, first_x, first_y, first_a, first_b, s.fills, s.borders, s.drawn, s.built, s.masks, s.declined);
  return 0;
}
`;

const out = path.join(root, 'build/tests/rounded-corners');
fs.mkdirSync(out, {recursive: true});
const source = path.join(out, 'test.cpp');
fs.writeFileSync(source, cpp);
const binary = path.join(out, process.platform === 'win32' ? 'test.exe' : 'test');
let result = spawnSync(host.cxx, [...host.flags, '-std=c++17', '-O1', '-w', source,
  path.join(root, 'src/ui/shared/rounded_corners.cpp'), path.join(root, 'src/ui/shared/kept_shadow.cpp'), host.archive, '-o', binary], {encoding: 'utf8'});
assert.equal(result.status, 0, result.stdout + result.stderr);
result = spawnSync(binary, [], {encoding: 'utf8'});
assert.equal(result.status, 0, result.stdout + result.stderr);
const run = JSON.parse(result.stdout.trim().split('\n').at(-1));
assert.equal(run.diffs, 0, `every pixel as LVGL draws it ${JSON.stringify(run)}`);
assert.ok(run.fills >= 30 * 8 && run.borders >= 30 * 5, `fills and lines came from kept corners ${JSON.stringify(run)}`);
assert.ok(run.built > 24 && run.masks <= 24, `old corner sets were replaced, at most 24 kept ${JSON.stringify(run)}`);
assert.equal(run.declined, 0, 'no task had to stay with LVGL');
assert.equal(run.drawn, run.fills + run.borders, `every marked task was drawn by the corner dispatch ${JSON.stringify(run)}`);
console.log(`PASS: rounded fills and lines from kept corners match LVGL pixel for pixel ${JSON.stringify(run)}`);
