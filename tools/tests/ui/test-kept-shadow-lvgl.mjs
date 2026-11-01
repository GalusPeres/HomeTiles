// A popup card's shadow cost 17-20 ms in every popup frame on the V2 (b603:
// 25 shadow tasks at 0.7 ms): LVGL copied and mirrored its cached corner blur
// for every band and blended the shadow's sides row by row. kept_shadow.cpp
// keeps that blur (taken from LVGL's own shadow cache) and draws each part of
// a band in one blend. Real LVGL with the firmware's shadow cache (72) draws
// popup-like cards with different radii, widths, spreads, offsets and
// opacities, in full-width bands and in narrow areas (tall bands), first
// alone and then with the kept shadows: every pixel must be LVGL's. A corner
// too large for LVGL's cache and a translucent card stay LVGL's.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {lvglHost} from '../../lib/lvgl-host.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n?/g, '\n');

// The firmware's cache size, which the kept shadows depend on.
const conf = read('lv_conf.h');
assert.match(conf, /#else\n#define LV_DRAW_SW_SHADOW_CACHE_SIZE 72\n#endif/, 'the P4 panels keep 72 x 72 corners');
const corners = read('src/ui/shared/rounded_corners.cpp');
assert.ok(corners.includes('const kept_shadow::Set* shadow = task && !set ? kept_shadow::find(task) : nullptr;') &&
  corners.indexOf('if (!display_flush_gate::ready()) {') < corners.indexOf('kept_shadow::find(task)'),
  'the dispatch draws kept shadows behind the band-copy gate');

const host = await lvglHost(root, ['-DLV_DRAW_SW_SHADOW_CACHE_SIZE=72']);
if (!host) {
  console.log('SKIP: the kept shadow pixel run needs LVGL and a host compiler (source checks passed)');
  process.exit(0);
}

const cpp = String.raw`
#include <lvgl.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>
#include "src/ui/shared/kept_shadow.h"
#include "src/ui/shared/rounded_corners.h"

static constexpr int W = 1280, H = 800, kBand = 28;
static std::vector<uint16_t> g_screen(W * H);
static std::vector<uint16_t> g_band(W * kBand);

static void flush(lv_display_t* d, const lv_area_t* a, uint8_t* px) {
  const int w = a->x2 - a->x1 + 1;
  for (int y = a->y1; y <= a->y2; ++y)
    memcpy(&g_screen[y * W + a->x1], reinterpret_cast<uint16_t*>(px) + (y - a->y1) * w, w * 2);
  lv_display_flush_ready(d);
}

struct Card { int x, y, w, h, radius, width, spread, ofs_x, ofs_y; lv_opa_t opa; uint32_t color; lv_opa_t bg_opa; };
static const Card kCards[] = {
  {240, 0, 800, 800, 38, 28, 3, 0, 0, LV_OPA_40, 0x000000, LV_OPA_COVER},   // the V2 popup card
  {40, 470, 300, 200, 22, 28, 2, 0, 6, 150, 0x202040, LV_OPA_COVER},        // smaller, offset
  {930, 560, 260, 180, 31, 40, 0, 4, 4, LV_OPA_COVER, 0x101010, LV_OPA_COVER},  // opaque shadow, corner 71
  {1060, 40, 180, 140, 30, 50, 2, 0, 0, LV_OPA_50, 0x000000, LV_OPA_COVER},  // corner 80: LVGL's
  {60, 60, 150, 120, 20, 20, 1, 0, 0, LV_OPA_60, 0x000000, 200},              // translucent: LVGL's
};

static lv_obj_t* g_cards[sizeof(kCards) / sizeof(kCards[0])];

static void build() {
  lv_obj_t* screen = lv_obj_create(nullptr);
  lv_obj_remove_style_all(screen);
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x0B0D10), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  // Tiles under the cards, so the shadow blends over different colors.
  for (int i = 0; i < 24; ++i) {
    lv_obj_t* tile = lv_obj_create(screen);
    lv_obj_remove_style_all(tile);
    lv_obj_set_pos(tile, 18 + (i % 6) * 210, 128 + (i / 6) * 168);
    lv_obj_set_size(tile, 194, 152);
    lv_obj_set_style_bg_color(tile, lv_color_hex(0x305070 + i * 0x0B0907), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
  }
  int i = 0;
  for (const Card& c : kCards) {
    lv_obj_t* card = lv_obj_create(screen);
    lv_obj_remove_style_all(card);
    lv_obj_set_pos(card, c.x, c.y);
    lv_obj_set_size(card, c.w, c.h);
    lv_obj_set_style_radius(card, c.radius, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_bg_opa(card, c.bg_opa, 0);
    lv_obj_set_style_shadow_width(card, c.width, 0);
    lv_obj_set_style_shadow_spread(card, c.spread, 0);
    lv_obj_set_style_shadow_offset_x(card, c.ofs_x, 0);
    lv_obj_set_style_shadow_offset_y(card, c.ofs_y, 0);
    lv_obj_set_style_shadow_opa(card, c.opa, 0);
    lv_obj_set_style_shadow_color(card, lv_color_hex(c.color), 0);
    g_cards[i++] = card;
  }
  lv_screen_load(screen);
}

// The frames compared: the whole screen, the popup card's area (taller
// bands), a narrow strip over its right side (bands taller than the kept
// strips), a corner piece and the smaller cards.
static const lv_area_t kAreas[] = {
  {0, 0, W - 1, H - 1},
  {200, 0, 1079, 799},
  {1010, 0, 1079, 799},
  {990, 740, 1075, 799},
  {0, 430, 420, 720},
  {880, 520, 1279, 799},
};
constexpr int kAreaCount = sizeof(kAreas) / sizeof(kAreas[0]);

static double render(const lv_area_t& area) {
  lv_obj_invalidate_area(lv_screen_active(), &area);
  const auto start = std::chrono::steady_clock::now();
  lv_refr_now(nullptr);
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

int main() {
  lv_init();
  lv_display_t* display = lv_display_create(W, H);
  lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(display, g_band.data(), nullptr, g_band.size() * 2, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(display, flush);
  build();

  std::vector<std::vector<uint16_t>> lvgl(kAreaCount);
  double lvgl_ms = 0;
  for (int i = 0; i < kAreaCount; ++i) {
    lvgl_ms += render(kAreas[i]);
    lvgl[i] = g_screen;
  }

  // The dispatch (rounded_corners.cpp) draws kept shadows from now on; an
  // object of its own installs it without changing the cards.
  lv_obj_t* anchor = lv_obj_create(lv_screen_active());
  lv_obj_remove_style_all(anchor);
  lv_obj_set_size(anchor, 1, 1);
  lv_obj_add_flag(anchor, LV_OBJ_FLAG_HIDDEN);
  rounded_corners::attach(anchor);

  unsigned diffs[2][kAreaCount] = {};
  double kept_ms[2] = {};
  for (int round = 0; round < 2; ++round) {
    for (int i = 0; i < kAreaCount; ++i) {
      kept_ms[round] += render(kAreas[i]);
      for (size_t p = 0; p < g_screen.size(); ++p) diffs[round][i] += g_screen[p] != lvgl[i][p];
    }
  }
  const kept_shadow::Stats stats = kept_shadow::stats();
  printf("{\"diffs\":[");
  for (int round = 0; round < 2; ++round)
    for (int i = 0; i < kAreaCount; ++i) printf("%s%u", round || i ? "," : "", diffs[round][i]);
  printf("],\"drawn\":%u,\"lvgl\":%u,\"sets\":%u,\"built\":%u,\"bytes\":%u,\"lvglMs\":%.2f,\"keptMs\":%.2f}\n",
         stats.drawn, stats.lvgl, stats.sets, stats.built, stats.bytes, lvgl_ms, kept_ms[1]);
  return 0;
}
`;

const out = path.join(root, 'build/tests/kept-shadow');
fs.mkdirSync(out, {recursive: true});
const source = path.join(out, 'test.cpp');
fs.writeFileSync(source, cpp);
const binary = path.join(out, process.platform === 'win32' ? 'test.exe' : 'test');
let result = spawnSync(host.cxx, [...host.flags, '-std=c++17', '-O1', '-w', source,
  path.join(root, 'src/ui/shared/rounded_corners.cpp'), path.join(root, 'src/ui/shared/kept_shadow.cpp'),
  path.join(root, 'src/core/display/display_flush_gate.cpp'), host.archive, '-o', binary], {encoding: 'utf8'});
assert.equal(result.status, 0, result.stdout + result.stderr);
result = spawnSync(binary, [], {encoding: 'utf8'});
assert.equal(result.status, 0, result.stdout + result.stderr);
const run = JSON.parse(result.stdout.trim().split('\n').at(-1));
assert.ok(run.diffs.every(d => d === 0), `every pixel as LVGL draws the shadows ${JSON.stringify(run)}`);
assert.ok(run.drawn > 0 && run.built >= 3, `the cached corners are kept and drawn ${JSON.stringify(run)}`);
assert.ok(run.lvgl > 0, 'the large corner and the translucent card stay with LVGL');
assert.ok(run.sets <= 4, 'at most four shadows kept');
console.log(`PASS: kept shadows draw LVGL's pixels ${JSON.stringify(run)}`);
