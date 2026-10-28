// After a page turn the power flow dots stood about 5 s before they moved
// (user 2026-10-28, S3 b607). The dots run on the display's refresh: each
// frame steps them and asks for the next by invalidating the dot's areas.
// lv_obj_invalidate_area drops an area that is not visible yet without a
// word, and the step counted it as drawn: no area, no next frame, the dots
// waited for the next state. A refused area now keeps the dot where it is
// drawn and asks for the next frame, and a stop that lasted 500 ms logs
// why once the dots move again.
import assert from 'node:assert/strict';
import {readRepoFile} from '../../lib/admin-source.mjs';

const renderer = readRepoFile('src/types/energy_flow/renderer.cpp').replace(/\r\n?/g, '\n');
const between = (from, to) => {
  const a = renderer.indexOf(from);
  const b = renderer.indexOf(to, a + 1);
  assert.ok(a >= 0 && b > a, `${from} .. ${to}`);
  return renderer.slice(a, b);
};

const redraw = between('DotRedraw invalidate_dot(', 'bool any_live(');
assert.ok(renderer.includes('enum class DotRedraw : uint8_t { kDrawn, kCovered, kRefused };'));
assert.ok(redraw.includes('if (!display || !lv_display_is_invalidation_enabled(display) || !lv_obj_area_is_visible(st.canvas, &visible)) {') &&
  redraw.includes('refused = true;'), 'an area LVGL would drop counts as refused');
assert.ok(redraw.indexOf('refused = true;') < redraw.indexOf('lv_obj_invalidate_area(st.canvas, &area);'),
  'checked before the invalidation');
assert.ok(redraw.includes('if (refused) return DotRedraw::kRefused;') &&
  redraw.includes('return any ? DotRedraw::kDrawn : DotRedraw::kCovered;'));

const step = between('uint32_t step_dots(bool& waiting, const char*& still) {', 'struct DrawProbe {');
const refusedAt = step.indexOf('if (redraw == DotRedraw::kRefused) {');
assert.ok(refusedAt >= 0 && refusedAt < step.indexOf('st->dot[line] = next;'),
  'a refused step keeps the dot where it is drawn');
assert.ok(step.slice(refusedAt, step.indexOf('st->dot[line] = next;')).includes('waiting = true;'),
  'and asks for the next frame');
for (const reason of ['"popup"', '"sleep"', '"screensaver"', '"hidden"', '"not visible"', '"no flow"']) {
  assert.ok(step.includes(`still = ${reason};`), `reason ${reason}`);
}

// The log: once per stop of at least kStillLogMs, English, outside the frame.
assert.ok(renderer.includes('constexpr uint32_t kStillLogMs = 500;'));
const log = between('void log_still(uint32_t now_ms) {', 'void motion_display_cb(');
assert.ok(log.includes('if (!g_still_since_ms || now_ms - g_still_since_ms < kStillLogMs) return;') &&
  log.includes('"[EnergyFlow] Dots move again after %lu ms (last frame: %s, woken by %s)\\n"'));
const frame = between('void motion_display_cb(', 'void wake_motion(');
assert.ok(frame.includes('if (!g_still_since_ms || still != g_still_reason) g_still_since_ms = now_ms | 1;'),
  'the stop counts from the frame whose reason changed');
assert.ok(frame.indexOf('log_still(now_ms);') > frame.indexOf('if (!moved) {'), 'logged when the dots move again');
assert.ok(between('void wake_motion(', 'void count_piece(').includes('g_woken_by_state = true;'));

console.log('Power flow dots ask for the next frame when LVGL refuses a step and log a long stop');
