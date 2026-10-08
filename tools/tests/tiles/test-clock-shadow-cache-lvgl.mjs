// The screensaver clock's soft shadow (nine faint dark copies per line) cost
// about 100 ms of every screensaver opening on the V2 (b304). The copies now
// sit in a hidden group drawn once per text change into an ARGB8888 picture
// (src/types/clock/clock_shadow_cache.h). On real LVGL: the picture behind
// the white text gives the same shadow as the copies drawn directly (within
// rounding), the hidden group is not drawn a second time, and a new picture
// replaces the old one. The clock renderer wires it as described.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {lvglHost} from '../../lib/lvgl-host.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n/g, '\n');

// The renderer: copies in a hidden group, the picture behind the main label,
// refreshed after the alignment, released before the children are deleted.
const renderer = read('src/types/clock/renderer.cpp');
const line = cppFunctionDefinitions(renderer).find(f => f.name === 'create_clock_line').source;
assert.match(line, /if \(!config\.fill_parent && shadow_out\) \{\s*group = lv_obj_create\(line\);/);
assert.match(line, /lv_obj_add_flag\(group, LV_OBJ_FLAG_HIDDEN\);/);
assert.match(line, /lv_label_create\(group \? group : line\)/);
assert.ok(line.indexOf('picture = lv_image_create(line);') < line.lastIndexOf('lv_label_create(line)'),
  'the picture lies behind the white text');
const update = cppFunctionDefinitions(renderer).find(f => f.name === 'update_clock_labels').source;
assert.match(update, /apply_clock_line_alignment\(data\);[\s\S]*?data->time_shadows\.refresh_shadow\(\);\s*data->date_shadows\.refresh_shadow\(\);/);
assert.match(renderer, /data->time_shadows\.release_shadow\(\);\s*data->date_shadows\.release_shadow\(\);\s*delete data;/);
assert.match(renderer, /void size_shadow_group\(lv_coord_t width, lv_coord_t height\) \{\s*if \(shadow_group\) lv_obj_set_size\(shadow_group, width \+ shadow_offset, height \+ shadow_offset\);/);

const host = await lvglHost(root);
if (!host) {
  console.log('SKIP: the clock shadow cache test needs LVGL and a host compiler');
  process.exit(0);
}
const out = path.join(root, 'build/tests/clock-shadow-cache');
fs.mkdirSync(out, {recursive: true});
const source = path.join(out, 'test.cpp');
fs.writeFileSync(source, `
#include <cstdio>
#include <cstdlib>
#include <lvgl.h>
#include "src/types/clock/clock_shadow_cache.h"
extern "C" { LV_FONT_DECLARE(ui_font_96); }

// The renderer's copies at the 1280x800 scale.
struct Copy { int x, y; lv_opa_t opa; };
static const Copy kCopies[9] = {{4, 4, 34}, {2, 4, 14}, {6, 4, 14}, {4, 2, 14}, {4, 6, 14},
                                {2, 2, 8}, {6, 2, 8}, {2, 6, 8}, {6, 6, 8}};
static const int kOffset = 6;

struct Line { lv_obj_t* root; lv_obj_t* group; lv_obj_t* image; lv_draw_buf_t* buf; };

static Line make_line(lv_obj_t* parent, const char* text, bool cached) {
  Line l{};
  lv_point_t size;
  lv_text_get_size(&size, text, &ui_font_96, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  l.root = lv_obj_create(parent);
  lv_obj_remove_style_all(l.root);
  lv_obj_set_pos(l.root, 40, 30);
  lv_obj_set_size(l.root, size.x, size.y);
  lv_obj_add_flag(l.root, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  if (cached) {
    l.group = lv_obj_create(l.root);
    lv_obj_remove_style_all(l.group);
    lv_obj_set_pos(l.group, 0, 0);
    lv_obj_set_size(l.group, size.x + kOffset, size.y + kOffset);
    lv_obj_add_flag(l.group, LV_OBJ_FLAG_HIDDEN);
  }
  for (const Copy& c : kCopies) {
    lv_obj_t* copy = lv_label_create(l.group ? l.group : l.root);
    lv_obj_set_style_text_font(copy, &ui_font_96, 0);
    lv_obj_set_style_text_color(copy, lv_color_black(), 0);
    lv_obj_set_style_text_opa(copy, c.opa, 0);
    lv_obj_set_size(copy, size.x, size.y);
    lv_obj_set_pos(copy, c.x, c.y);
    lv_label_set_text(copy, text);
  }
  if (cached) {
    l.image = lv_image_create(l.root);
    lv_obj_set_pos(l.image, 0, 0);
  }
  lv_obj_t* text_label = lv_label_create(l.root);
  lv_obj_set_style_text_font(text_label, &ui_font_96, 0);
  lv_obj_set_style_text_color(text_label, lv_color_white(), 0);
  lv_obj_set_size(text_label, size.x, size.y);
  lv_label_set_text(text_label, text);
  if (cached) clock_shadow_cache::show(l.image, l.buf, clock_shadow_cache::render(l.group));
  return l;
}

static lv_draw_buf_t* frame(lv_obj_t* screen) {
  lv_refr_now(nullptr);
  return lv_snapshot_take(screen, LV_COLOR_FORMAT_XRGB8888);
}

int main() {
  lv_init();
  lv_display_t* display = lv_display_create(560, 200);
  static uint8_t buffer[560 * 200 * 4];
  lv_display_set_buffers(display, buffer, nullptr, sizeof(buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t*, uint8_t*) { lv_display_flush_ready(d); });
  lv_obj_t* screen = lv_screen_active();
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x6A8CAF), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

  Line direct = make_line(screen, "12:34", false);
  lv_draw_buf_t* a = frame(screen);
  lv_obj_delete(direct.root);
  Line cached = make_line(screen, "12:34", true);
  if (!cached.buf) { std::printf("no picture\\n"); return 1; }
  lv_draw_buf_t* b = frame(screen);

  int max_diff = 0, shadowed = 0, over3 = 0;
  long changed = 0, sum_diff = 0;
  const uint32_t bg = 0x6A8CAF;
  for (uint32_t y = 0; y < a->header.h; ++y) {
    const uint8_t* ra = a->data + y * a->header.stride;
    const uint8_t* rb = b->data + y * b->header.stride;
    for (uint32_t x = 0; x < a->header.w; ++x) {
      int px_diff = 0;
      for (int c = 0; c < 3; ++c) {
        const int d = std::abs(int(ra[x * 4 + c]) - int(rb[x * 4 + c]));
        if (d > px_diff) px_diff = d;
      }
      if (px_diff > max_diff) max_diff = px_diff;
      if (px_diff > 3) ++over3;
      if (px_diff) { ++changed; sum_diff += px_diff; }
      const uint32_t px = ra[x * 4] | (ra[x * 4 + 1] << 8) | (ra[x * 4 + 2] << 16);
      if (px != bg && ra[x * 4] < 0xA0) ++shadowed;  // darker than the background
    }
  }
  std::printf("max_diff %d shadowed %d over3 %d changed %ld mean_x100 %ld\\n", max_diff, shadowed,
              over3, changed, changed ? sum_diff * 100 / changed : 0);

  // A new text draws a new picture and frees the old one.
  lv_draw_buf_t* first = cached.buf;
  lv_obj_t* copy = lv_obj_get_child(cached.group, 0);
  for (uint32_t i = 0; i < lv_obj_get_child_count(cached.group); ++i) lv_label_set_text(lv_obj_get_child(cached.group, i), "12:35");
  (void)copy;
  clock_shadow_cache::show(cached.image, cached.buf, clock_shadow_cache::render(cached.group));
  std::printf("replaced %d\\n", cached.buf && cached.buf != first ? 1 : 0);
  clock_shadow_cache::release(cached.image, cached.buf);
  std::printf("released %d\\n", cached.buf == nullptr && lv_image_get_src(cached.image) == nullptr ? 1 : 0);
  return 0;
}
`);
const binary = path.join(out, 'clock-shadow' + (process.platform === 'win32' ? '.exe' : ''));
let run = spawnSync(host.cxx, [...host.flags, '-std=c++17', '-Wno-deprecated-declarations', source, host.archive, '-o', binary], {encoding: 'utf8'});
assert.equal(run.status, 0, run.stdout + run.stderr);
run = spawnSync(binary, [], {encoding: 'utf8'});
assert.equal(run.status, 0, run.stdout + run.stderr);
const diff = /max_diff (\d+) shadowed (\d+) over3 (\d+) changed (\d+) mean_x100 (\d+)/.exec(run.stdout);
assert.ok(diff, run.stdout);
assert.ok(Number(diff[2]) > 500, `the shadow darkens the background (${diff[2]} px)`);
// Nine faint layers composed in the picture's alpha first round a little
// differently than nine blends straight onto the background: about 2 of 255
// levels on average, at most 7 (XRGB8888 here; RGB565 on the panels keeps
// 5 or 6 bits). The hidden group drawn as well would double the shadow.
assert.ok(Number(diff[1]) <= 8, `same shadow as the copies (max channel difference ${diff[1]})`);
assert.ok(Number(diff[5]) <= 300, `same shadow on average (mean difference ${Number(diff[5]) / 100})`);
assert.ok(Number(diff[3]) * 20 < Number(diff[2]), `few pixels differ by more than 3 levels (${diff[3]})`);
assert.match(run.stdout, /replaced 1/);
assert.match(run.stdout, /released 1/);
console.log(`Clock shadow: one picture per text change, the same shadow as nine copies (max difference ${diff[1]})`);
