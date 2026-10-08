// The screensaver clock's soft shadow: nine faint dark copies of each line,
// each a label of the large compressed clock font, cost about 110 ms of
// every screensaver opening on the V2 (b304); one composed ARGB picture per
// opening cost 220 ms (b305, reverted), one composed A8 picture came out
// visibly lighter on RGB565 panels (b306 trial: LVGL rounds every copy down
// in 5 bits, which makes the shadow darker than its opacities). The text is
// now drawn once into an A8 mask and the nine copies are A8 images of it:
// LVGL blends them with the same mask blend as glyphs. On real LVGL the
// images give the copies' shadow in XRGB8888 and in the panels' RGB565; the
// renderer wires it as described and keeps masks across openings.
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
// The line: a hidden mask label and nine A8 images behind the white text;
// the copy labels only without them (full-width lines).
const line = fn('create_clock_line');
assert.match(line, /if \(!config\.fill_parent && shadow_out\) \{[\s\S]*?lv_obj_t\* mask = lv_label_create\(line\);/);
assert.match(line, /lv_obj_add_flag\(mask, LV_OBJ_FLAG_HIDDEN\);/);
assert.match(line, /lv_obj_set_style_image_recolor\(image, lv_color_black\(\), 0\);\s*lv_obj_set_style_image_opa\(image, copies\[i\]\.opa, 0\);/);
assert.match(line, /if \(!shadow_out \|\| !shadow_out->mask_label\) \{\s*for \(uint8_t i = 0; i < kClockShadowCopies; \+\+i\) \{/);
assert.ok(line.indexOf('lv_image_create(line)') < line.lastIndexOf('lv_obj_t* label = lv_label_create(line);'),
  'the copies lie behind the white text');
// The mask: from the cache when the line is unchanged, else drawn once.
const mask = fn('clock_shadow_mask');
assert.match(mask, /entry\.font == font && entry\.width == width && entry\.height == height &&\s*entry\.alignment == alignment && strcmp\(entry\.text, text\) == 0\) \{[\s\S]*?return lv_draw_buf_dup\(entry\.mask\);/);
assert.match(mask, /lv_snapshot_take\(mask_label, LV_COLOR_FORMAT_A8\)/);
assert.match(mask, /slot->ext = lv_obj_get_ext_draw_size\(mask_label\);/);
assert.match(renderer, /lv_image_set_src\(copy_images\[i\], mask\);\s*lv_obj_set_pos\(copy_images\[i\], copies\[i\]\.x - ext, copies\[i\]\.y - ext\);/);
assert.match(fn('update_clock_labels'), /if \(changed\) \{\s*apply_clock_line_alignment\(data\);[\s\S]*?time_shadows\.refresh_shadow\(\);\s*data->date_shadows\.refresh_shadow\(\);/);
assert.match(renderer, /data->time_shadows\.release_shadow\(\);\s*data->date_shadows\.release_shadow\(\);\s*delete data;/);

const host = await lvglHost(root);
if (!host) {
  console.log('SKIP: the clock shadow mask test needs LVGL and a host compiler');
  process.exit(0);
}
const out = path.join(root, 'build/tests/clock-shadow-mask');
fs.mkdirSync(out, {recursive: true});
const source = path.join(out, 'test.cpp');
fs.writeFileSync(source, `
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <lvgl.h>
extern "C" { LV_FONT_DECLARE(ui_font_96); }

// The renderer's copies at the 1280x800 scale.
struct Copy { int16_t x, y; lv_opa_t opa; };
static const Copy kCopies[9] = {
    {4, 4, 34}, {2, 4, 14}, {6, 4, 14}, {4, 2, 14}, {4, 6, 14}, {2, 2, 8}, {6, 2, 8}, {2, 6, 8}, {6, 6, 8}};

static lv_obj_t* label(lv_obj_t* parent, const char* text, lv_point_t size, lv_color_t color, lv_opa_t opa) {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, &ui_font_96, 0);
  lv_obj_set_style_text_color(l, color, 0);
  lv_obj_set_style_text_opa(l, opa, 0);
  lv_obj_set_size(l, size.x, size.y);
  lv_label_set_text(l, text);
  return l;
}

static lv_obj_t* make_line(lv_obj_t* screen, const char* text, bool images) {
  lv_point_t size;
  lv_text_get_size(&size, text, &ui_font_96, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  lv_obj_t* line = lv_obj_create(screen);
  lv_obj_remove_style_all(line);
  lv_obj_set_pos(line, 40, 30);
  lv_obj_set_size(line, size.x, size.y);
  lv_obj_add_flag(line, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
  if (!images) {
    for (const Copy& c : kCopies) lv_obj_set_pos(label(line, text, size, lv_color_black(), c.opa), c.x, c.y);
  } else {
    lv_obj_t* mask_label = label(line, text, size, lv_color_white(), LV_OPA_COVER);
    lv_obj_add_flag(mask_label, LV_OBJ_FLAG_HIDDEN);
    lv_draw_buf_t* mask = lv_snapshot_take(mask_label, LV_COLOR_FORMAT_A8);
    if (!mask) { std::printf("no mask\\n"); std::exit(1); }
    // The snapshot reaches beyond the label by its extra draw size.
    const int32_t ext = (static_cast<int32_t>(mask->header.w) - size.x) / 2;
    for (const Copy& c : kCopies) {
      lv_obj_t* image = lv_image_create(line);
      lv_obj_set_pos(image, c.x - ext, c.y - ext);
      lv_obj_set_style_image_recolor(image, lv_color_black(), 0);
      lv_obj_set_style_image_opa(image, c.opa, 0);
      lv_image_set_src(image, mask);
    }
  }
  label(line, text, size, lv_color_white(), LV_OPA_COVER);
  return line;
}

struct Diff { int max = 0; long changed = 0, shadowed = 0; };

static Diff compare(lv_draw_buf_t* a, lv_draw_buf_t* b) {
  Diff d;
  const bool rgb565 = a->header.cf == LV_COLOR_FORMAT_RGB565;
  for (uint32_t y = 0; y < a->header.h; ++y) {
    const uint8_t* ra = a->data + y * a->header.stride;
    const uint8_t* rb = b->data + y * b->header.stride;
    for (uint32_t x = 0; x < a->header.w; ++x) {
      int c[3][2];
      for (int k = 0; k < 2; ++k) {
        const uint8_t* r = k ? rb : ra;
        if (rgb565) {
          const uint16_t p = reinterpret_cast<const uint16_t*>(r)[x];
          c[0][k] = p >> 11; c[1][k] = (p >> 5) & 63; c[2][k] = p & 31;
        } else {
          c[0][k] = r[x * 4 + 2]; c[1][k] = r[x * 4 + 1]; c[2][k] = r[x * 4];
        }
      }
      int m = 0;
      for (auto& ch : c) m = std::max(m, std::abs(ch[0] - ch[1]));
      d.max = std::max(d.max, m);
      if (m) ++d.changed;
      if (!rgb565 && r_dark(c)) ++d.shadowed;
    }
  }
  return d;
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

  lv_obj_t* direct = make_line(screen, "12:34", false);
  lv_refr_now(nullptr);
  lv_draw_buf_t* a = lv_snapshot_take(screen, LV_COLOR_FORMAT_XRGB8888);
  lv_draw_buf_t* a565 = lv_snapshot_take(screen, LV_COLOR_FORMAT_RGB565);
  lv_obj_delete(direct);
  make_line(screen, "12:34", true);
  lv_refr_now(nullptr);
  lv_draw_buf_t* b = lv_snapshot_take(screen, LV_COLOR_FORMAT_XRGB8888);
  lv_draw_buf_t* b565 = lv_snapshot_take(screen, LV_COLOR_FORMAT_RGB565);

  const Diff x = compare(a, b);
  const Diff r = compare(a565, b565);
  std::printf("xrgb max %d changed %ld shadowed %ld\\n", x.max, x.changed, x.shadowed);
  std::printf("rgb565 max %d changed %ld\\n", r.max, r.changed);
  return 0;
}
`.replace('if (!rgb565 && r_dark(c)) ++d.shadowed;', 'if (!rgb565 && c[2][0] < 0xA0) ++d.shadowed;  // darker than the background'));
const binary = path.join(out, 'clock-shadow' + (process.platform === 'win32' ? '.exe' : ''));
let run = spawnSync(host.cxx, [...host.flags, '-std=c++17', '-Wno-deprecated-declarations', source, host.archive, '-o', binary],
  {encoding: 'utf8'});
assert.equal(run.status, 0, run.stdout + run.stderr);
run = spawnSync(binary, [], {encoding: 'utf8'});
assert.equal(run.status, 0, run.stdout + run.stderr);
const xrgb = /xrgb max (\d+) changed (\d+) shadowed (\d+)/.exec(run.stdout);
const rgb565 = /rgb565 max (\d+) changed (\d+)/.exec(run.stdout);
assert.ok(xrgb && rgb565, run.stdout);
assert.ok(Number(xrgb[3]) > 500, `the shadow darkens the background (${xrgb[3]} px)`);
assert.ok(Number(xrgb[1]) <= 1, `XRGB8888: the images give the copies' shadow (max ${xrgb[1]} of 255)`);
assert.ok(Number(rgb565[1]) <= 1, `RGB565: the images give the copies' shadow (max ${rgb565[1]} step)`);
console.log(`Clock shadow: nine A8 copies of one mask, the same shadow (XRGB max ${xrgb[1]}, RGB565 max ${rgb565[1]} step)`);
