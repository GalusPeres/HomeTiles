import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {lvglHost} from '../../lib/lvgl-host.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const host = await lvglHost(root);
if (!host) {
  console.log('SKIP: Tile press layout requires LVGL and a host compiler');
  process.exit(0);
}
const shared = fs.readFileSync(path.join(root, 'src/tiles/runtime/tile_renderer_shared.h'), 'utf8');
const helper = cppFunctionDefinitions(shared).find(fn => fn.name === 'disable_pressed_button_animation').source;
const surface = fs.readFileSync(path.join(root, 'src/ui/shared/ui_surface_style.cpp'), 'utf8')
  .replace('#include "src/core/config/config_manager.h"', '');
const out = path.join(root, 'build/tests/tile-press-layout');
fs.mkdirSync(out, {recursive: true});
const source = path.join(out, 'test.cpp');
const binary = path.join(out, process.platform === 'win32' ? 'test.exe' : 'test');
fs.writeFileSync(source, String.raw`
#include <lvgl.h>
#include <cassert>
#include <cstdio>
#include <vector>
struct TestConfig { bool tile_borders = false; };
struct TestConfigManager { TestConfig config; const TestConfig& getConfig() const { return config; } } configManager;
${surface}
${helper}
static int style_changes = 0;
static void count_style(lv_event_t*) { ++style_changes; }
int main() {
  lv_init();
  auto* display = lv_display_create(1280, 800);
  std::vector<uint16_t> band(1280 * 28);
  lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(display, band.data(), nullptr, band.size() * 2, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(display, [](lv_display_t* d, const lv_area_t*, uint8_t*) { lv_display_flush_ready(d); });
  lv_theme_default_init(display, lv_color_hex(0x26A69A), lv_color_hex(0xC14444), false, LV_FONT_DEFAULT);
  for (bool border : {false, true}) for (bool focused : {false, true}) for (int children : {2, 32}) {
    auto* card = lv_button_create(lv_screen_active());
    lv_obj_set_size(card, children == 2 ? 168 : 536, children == 2 ? 145 : 306);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x223355), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x324365), LV_STATE_PRESSED);
    disable_pressed_button_animation(card);
    std::vector<lv_obj_t*> labels;
    for (int i = 0; i < children; ++i) {
      auto* label = lv_label_create(card);
      lv_label_set_text(label, "23.8 C");
      lv_obj_set_pos(label, (i % 4) * 80, (i / 4) * 24);
      lv_obj_add_event_cb(label, count_style, LV_EVENT_STYLE_CHANGED, nullptr);
      labels.push_back(label);
    }
    // The grid applies global styles after the type renderer has created its tree.
    configManager.config.tile_borders = border;
    ui_surface_style::apply_global_tile_border(card);
    if (focused) lv_obj_add_state(card, LV_STATE_FOCUSED);
    lv_refr_now(display);
    std::vector<lv_area_t> original(labels.size());
    for (size_t i = 0; i < labels.size(); ++i) lv_obj_get_coords(labels[i], &original[i]);
    for (int cycle = 0; cycle < 3; ++cycle) {
      style_changes = 0;
      lv_obj_add_state(card, LV_STATE_PRESSED);
      std::printf("children=%d press_style_changes=%d\n", children, style_changes);
      assert(style_changes == 0 && "A stationary press must not invalidate descendant text layout");
      assert(lv_obj_get_style_translate_x(card, LV_PART_MAIN) == 0);
      assert(lv_obj_get_style_translate_y(card, LV_PART_MAIN) == 0);
      assert(lv_obj_get_style_border_width(card, LV_PART_MAIN) == 0);
      assert(lv_obj_get_style_outline_width(card, LV_PART_MAIN) == (border ? 1 : 0));
      assert(lv_obj_get_style_outline_pad(card, LV_PART_MAIN) == -1);
      assert(lv_obj_get_style_outline_opa(card, LV_PART_MAIN) == (border ? 51 : 0));
      lv_tick_inc(200); lv_timer_handler(); lv_refr_now(display);
      assert(style_changes == 0 && "Finishing the press transition must not invalidate descendant text layout");
      assert(lv_color_eq(lv_obj_get_style_bg_color(card, LV_PART_MAIN), lv_color_hex(0x324365)));
      style_changes = 0;
      lv_obj_remove_state(card, LV_STATE_PRESSED);
      assert(style_changes == 0 && "Release must not rebuild descendant layout before the click callback");
      lv_tick_inc(200); lv_timer_handler(); lv_refr_now(display);
      assert(style_changes == 0 && "Finishing the release transition must not invalidate descendant text layout");
      assert(lv_color_eq(lv_obj_get_style_bg_color(card, LV_PART_MAIN), lv_color_hex(0x223355)));
      for (size_t i = 0; i < labels.size(); ++i) {
        lv_area_t actual; lv_obj_get_coords(labels[i], &actual);
        assert(actual.x1 == original[i].x1 && actual.x2 == original[i].x2);
        assert(actual.y1 == original[i].y1 && actual.y2 == original[i].y2);
      }
    }
    lv_obj_delete(card);
  }
  lv_deinit();
}
`);
let result = spawnSync(host.cxx, [...host.flags, '-std=c++17', source, host.archive, '-o', binary], {encoding: 'utf8'});
assert.equal(result.status, 0, result.stdout + result.stderr);
result = spawnSync(binary, [], {encoding: 'utf8'});
assert.equal(result.status, 0, result.stdout + result.stderr);
console.log('Tile press: real global border styles, focus and repeated presses preserve layout, colors and outline');
