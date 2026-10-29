// The power flow dots stood 3-8 s after a page turn (user 2026-10-28; b610
// trace "first move 22 ms; frames 22m 8293m"): the frame that showed the page
// stepped them once and no frame followed until the next state. The dots
// step at LV_EVENT_REFR_START, after the refresh paused its timer, and their
// new area was meant to restart it. Real LVGL 9.6 shows why it did not: in
// that frame the whole grid is marked, lv_inv_area drops the dot's area
// inside it without a refresh request, and the timer stays paused. The frame
// callback now resumes the timer after a move itself (renderer.cpp
// motion_display_cb, checked by test-energy-flow-dots-resume.mjs).
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {lvglHost} from '../../lib/lvgl-host.mjs';

const root = path.resolve(import.meta.dirname, '../../..');
const renderer = fs.readFileSync(path.join(root, 'src/types/energy_flow/renderer.cpp'), 'utf8').replace(/\r\n/g, '\n');
assert.ok(renderer.includes('if (moved || waiting) lv_timer_resume(lv_display_get_refr_timer(g_motion_display));'),
  'the frame callback resumes the refresh after a move');

const host = await lvglHost(root);
if (!host) {
  console.log('SKIP: the next-frame run needs LVGL and a host compiler (source check passed)');
  process.exit(0);
}

const out = path.join(root, 'build/tests/energy-flow-next-frame');
fs.mkdirSync(out, {recursive: true});
const cpp = String.raw`
#include <cstdio>
#include <lvgl.h>
#include <lvgl_private.h>

static uint8_t g_buf[480 * 40 * 4];
static lv_obj_t* g_tile = nullptr;
static bool g_resume = false;
static int g_steps = 0;

static void flush_cb(lv_display_t* display, const lv_area_t*, uint8_t*) { lv_display_flush_ready(display); }

// A dot step as renderer.cpp does it: the dot's old and new place, small,
// inside the tile, in screen coordinates; with g_resume the frame callback
// also resumes the refresh like motion_display_cb.
static void refr_start(lv_event_t* e) {
  ++g_steps;
  lv_area_t tile;
  lv_obj_get_coords(g_tile, &tile);
  lv_area_t dot = {tile.x1 + 20 + g_steps, tile.y1 + 30, tile.x1 + 31 + g_steps, tile.y1 + 41};
  lv_obj_invalidate_area(g_tile, &dot);
  if (g_resume) lv_timer_resume(lv_display_get_refr_timer(static_cast<lv_display_t*>(lv_event_get_target(e))));
}

int main() {
  lv_init();
  lv_display_t* display = lv_display_create(480, 480);
  lv_display_set_buffers(display, g_buf, nullptr, sizeof(g_buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(display, flush_cb);
  lv_obj_t* grid = lv_obj_create(lv_screen_active());
  lv_obj_set_size(grid, 480, 480);
  lv_obj_set_pos(grid, 0, 0);
  g_tile = lv_obj_create(grid);
  lv_obj_set_size(g_tile, 302, 254);
  lv_obj_set_pos(g_tile, 8, 100);
  lv_refr_now(display);
  lv_timer_t* timer = lv_display_get_refr_timer(display);
  lv_display_add_event_cb(display, refr_start, LV_EVENT_REFR_START, nullptr);
  int result[4];
  for (int resume = 0; resume < 2; ++resume) {
    g_resume = resume != 0;
    // Running dots: nothing else marked, the step's own area.
    lv_refr_now(display);
    result[resume * 2] = timer->paused ? 0 : 1;
    // The page shown again: its whole grid marked before the frame.
    lv_obj_invalidate(grid);
    lv_refr_now(display);
    result[resume * 2 + 1] = timer->paused ? 0 : 1;
  }
  printf("{\"running\":%d,\"shown\":%d,\"running_resumed\":%d,\"shown_resumed\":%d,\"steps\":%d}\n", result[0],
         result[1], result[2], result[3], g_steps);
  return 0;
}
`;

const source = path.join(out, 'test.cpp');
fs.writeFileSync(source, cpp);
const binary = path.join(out, process.platform === 'win32' ? 'test.exe' : 'test');
let result = spawnSync(host.cxx, [...host.flags, '-std=c++17', '-O1', '-w', source, host.archive, '-o', binary],
  {encoding: 'utf8'});
assert.equal(result.status, 0, result.stdout + result.stderr);
result = spawnSync(binary, [], {encoding: 'utf8'});
assert.equal(result.status, 0, result.stdout + result.stderr);
const run = JSON.parse(result.stdout.trim().split('\n').at(-1));
assert.equal(run.steps, 4, `each refresh stepped the dot once ${JSON.stringify(run)}`);
assert.equal(run.running, 1, `a step's own area asks for the next frame ${JSON.stringify(run)}`);
assert.equal(run.shown, 0, `in the frame that shows the page it does not: the dots stood (b610) ${JSON.stringify(run)}`);
assert.equal(run.running_resumed, 1, `resumed: the next frame comes while running ${JSON.stringify(run)}`);
assert.equal(run.shown_resumed, 1, `resumed: the next frame comes after the page is shown ${JSON.stringify(run)}`);
console.log(`PASS: a dot step resumes the refresh itself; its area alone does not in the frame that shows a page ${JSON.stringify(run)}`);
