// After a page turn the power flow dots stood about 5 s before they moved
// (user 2026-10-28, S3 b607/b608). The dots run on the display's refresh:
// each frame steps them and asks for the next by invalidating the dot's
// areas. Two gaps ended that chain until the next state arrived:
// - lv_obj_invalidate_area drops an area that is not visible yet without a
//   word, and the step counted it as drawn (b608: a refused step keeps the
//   dot and asks for the next frame);
// - a frame that found the tile hidden asked for nothing, and the page turn
//   that showed it again left no frame that stepped the dots (b608 log
//   "last frame: hidden"). A hidden flow, and one behind a popup, now keeps
//   asking for frames, so the dots go on with the first frame that shows it.
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

// A hidden flow and one behind a popup keep asking; sleep and the
// screensaver do not (nothing is drawn there).
const hiddenAt = step.indexOf('if (lv_obj_t* hider = st->card ? hidden_by(st->card) : nullptr) {');
assert.ok(hiddenAt >= 0, 'what hides the tile is looked up');
const hidden = step.slice(hiddenAt, step.indexOf('still = kStillWaiting;'));
assert.ok(hidden.includes('still = kStillHidden;') && hidden.includes('waiting = true;') && hidden.includes('continue;'),
  'a hidden flow asks for the next frame');
const popup = step.slice(step.indexOf('if (popup_shell_active()) {'), step.indexOf('if (powerManager.isInSleep()) {'));
assert.ok(popup.includes('still = kStillPopup;') && popup.includes('waiting = any_flow();'), 'so does one behind a popup');
for (const [from, to] of [['if (powerManager.isInSleep()) {', 'if (image_screensaver_covers_ui()) {'],
  ['if (image_screensaver_covers_ui()) {', 'uint32_t moved = 0;']]) {
  assert.ok(!step.slice(step.indexOf(from), step.indexOf(to)).includes('waiting'), `${from} asks for nothing`);
}
for (const reason of ['kStillNoFlow = "no flow"', 'kStillPopup = "popup"', 'kStillSleep = "sleep"', 'kStillScreensaver = "screensaver"',
  'kStillHidden = "hidden"', 'kStillWaiting = "waiting"', 'kStillRefused = "not visible"']) {
  assert.ok(renderer.includes(`constexpr const char* ${reason};`), reason);
}

// The logs: what hides the tile once per hiding, and a stall with the tile
// shown once it lasted kStillLogMs; both outside the frame callback.
assert.ok(renderer.includes('constexpr uint32_t kStillLogMs = 500;'));
const note = between('void note_still(const char* still, uint32_t now_ms) {', 'void log_still(');
assert.ok(note.includes('if (still != g_still_reason && still == kStillHidden) {') &&
  note.includes('"[EnergyFlow] Dots wait: tile hidden by %s %ldx%ld, %u levels up\\n"'), 'what hides it, once');
assert.ok(note.includes('const bool stalled = still == kStillWaiting || still == kStillRefused;') &&
  note.includes('if (!stalled) g_still_since_ms = 0;'), 'time on another page is no stall');
const log = between('void log_still(uint32_t now_ms) {', 'void motion_display_cb(');
assert.ok(log.includes('if (!g_still_since_ms || now_ms - g_still_since_ms < kStillLogMs) return;') &&
  log.includes('"[EnergyFlow] Dots move again after %lu ms (last frame: %s, woken by %s)\\n"'));
const frame = between('void motion_display_cb(', 'void wake_motion(');
assert.ok(frame.includes('note_still(still, now_ms);') &&
  frame.indexOf('note_still(still, now_ms);') < frame.indexOf('if (waiting) lv_timer_resume(lv_display_get_refr_timer(g_motion_display));'));
assert.ok(frame.indexOf('log_still(now_ms);') > frame.indexOf('if (!moved) {'), 'logged when the dots move again');
assert.ok(between('void wake_motion(', 'void count_piece(').includes('g_woken_by_state = true;'));

console.log('Power flow dots keep asking for frames while hidden and resume with the first frame that shows them');
