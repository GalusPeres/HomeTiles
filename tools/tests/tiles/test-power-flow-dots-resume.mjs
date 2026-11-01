// After a page turn the power flow dots stood about 5 s before they moved
// (user 2026-10-28, S3 b607/b608). The dots run on the display's refresh:
// each frame steps them and asks for the next by invalidating the dot's
// areas. Two gaps ended that chain until the next state arrived:
// - lv_obj_invalidate_area drops an area that is not visible yet without a
//   word, and the step counted it as drawn (b608: a refused step keeps the
//   dot and asks for the next frame);
// - the frame that shows a page again stepped the dots once and asked for no
//   next frame: its whole grid was marked, and lv_inv_area drops the dot's
//   area inside it without a refresh request (b610 trace "22m 8293m"). The
//   frame callback now asks for the next frame after a move itself
//   (test-power-flow-next-frame-lvgl.mjs shows it with real LVGL). b609's
//   frames while hidden or behind a popup are gone again.
import assert from 'node:assert/strict';
import {readRepoFile} from '../../lib/admin-source.mjs';

const renderer = readRepoFile('src/types/power_flow/renderer.cpp').replace(/\r\n?/g, '\n');
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

// Hidden, behind a popup, asleep or under the screensaver nothing asks for
// frames (no work in the background, user 2026-10-28).
const hiddenAt = step.indexOf('if (lv_obj_t* hider = st->card ? hidden_by(st->card) : nullptr) {');
assert.ok(hiddenAt >= 0, 'what hides the tile is looked up');
const hidden = step.slice(hiddenAt, step.indexOf('still = kStillWaiting;'));
assert.ok(hidden.includes('still = kStillHidden;') && hidden.includes('continue;'));
for (const [from, to] of [['if (popup_shell_active()) {', 'if (powerManager.isInSleep()) {'],
  ['if (powerManager.isInSleep()) {', 'if (image_screensaver_covers_ui()) {'],
  ['if (image_screensaver_covers_ui()) {', 'uint32_t moved = 0;']]) {
  assert.ok(!step.slice(step.indexOf(from), step.indexOf(to)).includes('waiting'), `${from} asks for nothing`);
}
assert.ok(!hidden.includes('waiting'), 'a hidden flow asks for nothing');
assert.ok(!renderer.includes('any_flow'), 'no frames kept running for a hidden flow');
for (const reason of ['kStillNoFlow = "no flow"', 'kStillPopup = "popup"', 'kStillSleep = "sleep"', 'kStillScreensaver = "screensaver"',
  'kStillHidden = "hidden"', 'kStillWaiting = "waiting"', 'kStillRefused = "not visible"']) {
  assert.ok(renderer.includes(`constexpr const char* ${reason};`), reason);
}

// The logs: what hides the tile once per hiding, and a stall with the tile
// shown once it lasted kStillLogMs; both outside the frame callback.
assert.ok(renderer.includes('constexpr uint32_t kStillLogMs = 500;'));
const note = between('void note_still(const char* still, uint32_t now_ms) {', 'void log_still(');
assert.ok(note.includes('if (still != g_still_reason && still == kStillHidden) {') &&
  note.includes('"[PowerFlow] Dots wait: tile hidden by %s %ldx%ld, %u levels up\\n"'), 'what hides it, once');
assert.ok(note.includes('const bool stalled = still == kStillWaiting || still == kStillRefused;') &&
  note.includes('if (!stalled) g_still_since_ms = 0;'), 'time on another page is no stall');
const log = between('void log_still(uint32_t now_ms) {', 'void motion_display_cb(');
assert.ok(log.includes('if (!g_still_since_ms || now_ms - g_still_since_ms < kStillLogMs) return;') &&
  log.includes('"[PowerFlow] Dots move again after %lu ms (last frame: %s, woken by %s)\\n"'));
const frame = between('void motion_display_cb(', 'void wake_motion(');
const resumeAt = frame.indexOf('if (moved || waiting) lv_timer_resume(lv_display_get_refr_timer(g_motion_display));');
assert.ok(resumeAt >= 0 && resumeAt < frame.indexOf('if (!moved) {'),
  'a frame that moved a dot asks for the next one itself, not through the dot\'s area');
assert.ok(frame.includes('note_still(still, now_ms);'));
assert.ok(frame.indexOf('log_still(now_ms);') > frame.indexOf('if (!moved) {'), 'logged when the dots move again');
assert.ok(between('void wake_motion(', 'void count_piece(').includes('g_woken_by_state = true;'));

// b610: b609 still stood about a second after a page turn and the log had no
// clock for it. Every place that shows a page, folder grid or the tiles' tab
// tells the flow at once; its first frames are traced (when they came, what
// they found) and logged once per showing, outside the frame callback.
assert.ok(frame.includes('trace_frame(moved, still, now_ms);') &&
  frame.indexOf('trace_frame(moved, still, now_ms);') < frame.indexOf('if (!moved) {'), 'every frame is traced');
assert.ok(!frame.includes('Serial'), 'no log inside the frame callback');
const trace = between('void trace_frame(uint32_t moved, const char* still, uint32_t now_ms) {', '// The display\'s refresh:');
assert.ok(trace.includes('if (t.moves >= kShownMoves || t.count >= kShownFrames || at >= kShownTraceMs) log_shown();'),
  'one line per showing, bounded');
const shownFn = renderer.slice(renderer.indexOf('void power_flow_shown(lv_obj_t* shown) {'));
assert.ok(shownFn.includes('if (lv_obj_t* hider = hidden_by(st->card)) {') &&
  shownFn.includes('lv_timer_resume(lv_display_get_refr_timer(g_motion_display));'),
  'records what hid the tile at the showing and asks for the next frame');
const tiles = readRepoFile('src/ui/tabs/tiles/tab_tiles_unified.cpp').replace(/\r\n?/g, '\n');
const shows = tiles.split('lv_obj_clear_flag(target->grid, LV_OBJ_FLAG_HIDDEN);\n').slice(1);
assert.equal(shows.length, 2, 'the cached and the built folder switch');
for (const after of shows) assert.ok(after.trimStart().startsWith('power_flow_shown(target->grid);'), 'right after the grid is shown');
const ui = readRepoFile('src/ui/ui_manager.cpp').replace(/\r\n?/g, '\n');
assert.ok(ui.includes('lv_obj_clear_flag(tab_panels[index], LV_OBJ_FLAG_HIDDEN);\n    if (index == 0) power_flow_shown(tab_panels[index]);'),
  'and the tiles\' tab coming back from Settings');

console.log('Power flow dots keep asking for frames while hidden and resume with the first frame that shows them');
