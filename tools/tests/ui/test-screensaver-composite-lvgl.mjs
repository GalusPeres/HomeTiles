// The screensaver frame takes the wallpaper with one copy and lets LVGL draw
// only what lies above it (src/ui/screensaver/composite_over_image.h):
// LVGL's own image copy of 1280x800 took 130-143 ms on the V2 (b304/b305).
// On real LVGL the frame equals lv_snapshot_take_to_draw_buf of the top layer
// pixel for pixel (wallpaper, a see-through card with radius and shadow, a
// label, a later top-layer object), and the copy is refused whenever LVGL
// would not draw the picture as a plain copy.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {lvglHost} from '../../lib/lvgl-host.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const host = await lvglHost(root);
if (!host) {
  console.log('SKIP: the screensaver composite test needs LVGL and a host compiler');
  process.exit(0);
}
const out = path.join(root, 'build/tests/screensaver-composite');
fs.mkdirSync(out, {recursive: true});
const source = path.join(out, 'test.cpp');
fs.writeFileSync(source, `
#include <cstdio>
#include <cstring>
#include <vector>
#include "src/ui/screensaver/composite_over_image.h"
extern "C" { LV_FONT_DECLARE(ui_font_40); }

static const int W = 320, H = 200;

static bool same(lv_draw_buf_t* a, lv_draw_buf_t* b) {
  for (int y = 0; y < H; ++y) {
    if (memcmp(a->data + y * a->header.stride, b->data + y * b->header.stride, W * 2) != 0) return false;
  }
  return true;
}

int main() {
  lv_init();
  lv_display_t* display = lv_display_create(W, H);
  static uint8_t buffer[W * H * 4];
  lv_display_set_buffers(display, buffer, nullptr, sizeof(buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t*, uint8_t*) { lv_display_flush_ready(d); });

  // The wallpaper: native RGB565, the frame's size and stride.
  static std::vector<uint16_t> pixels(W * H);
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x)
      pixels[y * W + x] = static_cast<uint16_t>(((x * 31 / W) << 11) | ((y * 63 / H) << 5) | ((x + y) & 31));
  lv_image_dsc_t picture{};
  picture.header.magic = LV_IMAGE_HEADER_MAGIC;
  picture.header.cf = LV_COLOR_FORMAT_RGB565;
  picture.header.w = W;
  picture.header.h = H;
  picture.header.stride = W * 2;
  picture.data_size = W * H * 2;
  picture.data = reinterpret_cast<const uint8_t*>(pixels.data());

  lv_obj_t* top = lv_layer_top();
  lv_obj_t* overlay = lv_obj_create(top);
  lv_obj_remove_style_all(overlay);
  lv_obj_set_size(overlay, W, H);
  lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
  lv_obj_t* image = lv_image_create(overlay);
  lv_obj_set_pos(image, 0, 0);
  lv_image_set_src(image, &picture);
  lv_obj_t* card = lv_obj_create(overlay);
  lv_obj_remove_style_all(card);
  lv_obj_set_pos(card, 30, 110);
  lv_obj_set_size(card, 140, 70);
  lv_obj_set_style_radius(card, 22, 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(0x1A1A1A), 0);
  lv_obj_set_style_bg_opa(card, 150, 0);
  lv_obj_set_style_shadow_width(card, 32, 0);
  lv_obj_set_style_shadow_spread(card, 3, 0);
  lv_obj_set_style_shadow_opa(card, 153, 0);
  lv_obj_t* text = lv_label_create(overlay);
  lv_obj_set_style_text_font(text, &ui_font_40, 0);
  lv_obj_set_style_text_color(text, lv_color_white(), 0);
  lv_label_set_text(text, "12:34");
  lv_obj_set_pos(text, 180, 20);
  lv_obj_t* later = lv_obj_create(top);  // above the overlay, like a popup
  lv_obj_remove_style_all(later);
  lv_obj_set_pos(later, 250, 140);
  lv_obj_set_size(later, 50, 40);
  lv_obj_set_style_bg_color(later, lv_color_hex(0x26A69A), 0);
  lv_obj_set_style_bg_opa(later, 200, 0);

  lv_draw_buf_t* a = lv_draw_buf_create(W, H, LV_COLOR_FORMAT_RGB565, W * 2);
  lv_draw_buf_t* b = lv_draw_buf_create(W, H, LV_COLOR_FORMAT_RGB565, W * 2);
  lv_snapshot_take_to_draw_buf(top, LV_COLOR_FORMAT_RGB565, a);
  memset(b->data, 0x5A, W * H * 2);
  const bool used = composite_over_image::render(display, top, image, &picture, b);
  std::printf("used %d same %d\\n", used ? 1 : 0, used && same(a, b) ? 1 : 0);

  // Refused when LVGL would not copy the picture as it is.
  lv_obj_set_style_image_recolor_opa(image, 80, 0);
  std::printf("recolored %d\\n", composite_over_image::render(display, top, image, &picture, b) ? 1 : 0);
  lv_obj_set_style_image_recolor_opa(image, LV_OPA_TRANSP, 0);
  lv_obj_set_style_image_opa(image, 200, 0);
  std::printf("see-through %d\\n", composite_over_image::render(display, top, image, &picture, b) ? 1 : 0);
  lv_obj_set_style_image_opa(image, LV_OPA_COVER, 0);
  lv_obj_add_flag(image, LV_OBJ_FLAG_HIDDEN);
  std::printf("hidden %d\\n", composite_over_image::render(display, top, image, &picture, b) ? 1 : 0);
  lv_obj_remove_flag(image, LV_OBJ_FLAG_HIDDEN);
  picture.header.cf = LV_COLOR_FORMAT_RGB565_SWAPPED;
  std::printf("swapped %d\\n", composite_over_image::render(display, top, image, &picture, b) ? 1 : 0);
  return 0;
}
`);
const binary = path.join(out, 'composite' + (process.platform === 'win32' ? '.exe' : ''));
let run = spawnSync(host.cxx, [...host.flags, '-std=c++17', '-Wno-deprecated-declarations', source, host.archive, '-o', binary],
  {encoding: 'utf8'});
assert.equal(run.status, 0, run.stdout + run.stderr);
run = spawnSync(binary, [], {encoding: 'utf8'});
assert.equal(run.status, 0, run.stdout + run.stderr);
assert.match(run.stdout, /used 1 same 1/, `the copied frame equals LVGL's snapshot\n${run.stdout}`);
for (const refused of ['recolored', 'see-through', 'hidden', 'swapped']) {
  assert.match(run.stdout, new RegExp(`${refused} 0`), `${refused}: the snapshot is taken instead`);
}
console.log('Screensaver composite: the wallpaper copy plus LVGL above it equals the snapshot pixel for pixel');
