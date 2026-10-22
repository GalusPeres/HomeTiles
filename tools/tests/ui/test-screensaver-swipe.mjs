// The screensaver beside Home (user 2026-10-22): on Home, the first page, a
// swipe from left to right opens the screensaver like its edge swipe; over
// the screensaver a swipe from right to left goes back to Home. A tap on its
// free background still closes it and its tiles keep their taps.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n/g, '\n');
const slice = (text, start, end) => {
  const from = text.indexOf(start);
  assert.ok(from >= 0, 'missing ' + start);
  const to = text.indexOf(end, from + start.length);
  assert.ok(to > from, 'missing end ' + end);
  return text.slice(from, to + end.length);
};

const manager = read('src/ui/ui_manager.cpp');
const saver = read('src/ui/screensaver/image_screensaver.cpp');

// The way in: only on Home (page 0) and only towards before it; the overview
// closes under the screensaver, other pages turn as before.
const turn = slice(manager, 'void UIManager::turnPage(int step) {', '\n}\n');
assert.match(turn, /if \(page == 0 && step < 0\) \{\s*page_preview::close\(\);\s*Serial\.println\("\[Swipe\] Home -> screensaver"\);\s*show_image_screensaver\(\);\s*return;\s*\}\s*if \(page >= 0\) showPage\(page \+ step\);/);
// Like the edge swipe it opens the screensaver also when it never starts on
// its own (the auto start is no condition).
assert.doesNotMatch(turn, /auto_screensaver/);
const action = slice(manager, 'void UIManager::runSwipeAction(uint8_t action) {', '\n}\n');
assert.match(action, /case swipe_gestures::kScreensaver:\s*show_image_screensaver\(\);/);

// The overlay is known while it is shown.
assert.match(read('src/ui/screensaver/image_screensaver.h'), /lv_obj_t\* image_screensaver_overlay\(\);/);
assert.match(slice(saver, 'lv_obj_t* image_screensaver_overlay() {', '\n}\n'), /return g_state \? g_state->overlay : nullptr;/);

// The way back: a press on the screensaver, on any tab, arms only its own
// swipe; a control that drags itself keeps its gesture.
const press = slice(manager, 'void UIManager::settings_gesture_indev_event_cb(lv_event_t* event) {', '\n}\n');
const armed = slice(press, 'if (lv_obj_t* saver = image_screensaver_overlay()) {', '\n      return;\n    }\n');
assert.ok(press.indexOf('image_screensaver_overlay()') < press.indexOf('const uint8_t tab = self->active_tab_index;'),
  'The screensaver is checked before the tab');
assert.match(armed, /if \(object == saver\) \{\s*on_saver = true;\s*break;\s*\}\s*if \(!lv_obj_has_flag\(object, LV_OBJ_FLAG_GESTURE_BUBBLE\)\) drags_itself = true;/);
assert.match(armed, /if \(!on_saver \|\| drags_itself\) return;/);
assert.match(armed, /self->access_gesture_action = kScreensaverSwipe;/);
assert.match(armed, /self->access_gesture_eligible = true;/);
assert.match(armed, /lv_obj_add_event_cb\(pressed_object, settings_gesture_pressed_object_event_cb, LV_EVENT_PRESSING, self\);/);
// Its taps stay as they are: no touch watching that would swallow them.
assert.doesNotMatch(armed, /touch_watched|lv_indev_stop_processing/);
// Its own marker beside the page swipe's.
assert.match(manager, /static constexpr uint8_t kPageSwipe = 0xFE;\s*\/\/[^\n]*\n\s*static constexpr uint8_t kScreensaverSwipe = 0xFD;/);

const motion = slice(manager, 'void UIManager::processSettingsGestureMotion(', '\n}\n');
assert.match(motion, /if \(access_gesture_action == kScreensaverSwipe\) \{\s*processScreensaverSwipeMotion\(input, released\);\s*return;\s*\}/);

// Right to left past the swipe distance, more sideways than up or down; the
// touch is let go of first (no tap on a tile or the background), then the
// screensaver closes and Home shows (from Settings too).
const back = slice(manager, 'void UIManager::processScreensaverSwipeMotion(lv_indev_t* input, bool released) {', '\n}\n');
assert.match(back, /if \(-dx < kSettingsGestureThreshold \|\| -dx <= abs_dy \|\| !is_image_screensaver_visible\(\)\) \{\s*if \(released\) \{\s*access_gesture_eligible = false;\s*detachSettingsGesturePressedObject\(\);\s*\}\s*return;\s*\}/);
assert.match(back, /if \(!released\) lv_indev_wait_release\(input\);\s*lv_indev_reset\(input, nullptr\);\s*Serial\.println\("\[Swipe\] screensaver -> Home"\);\s*hide_image_screensaver\(\);\s*runSwipeAction\(swipe_gestures::kHome\);/);
assert.match(action, /if \(active_tab_index == kSettingsTab && \(action == swipe_gestures::kHome \|\| action == swipe_gestures::kBack\)\) \{\s*settings_screen::close_overlays\(\);\s*if \(action == swipe_gestures::kHome\) switchToFolder\(root\);/);
assert.match(action, /case swipe_gestures::kHome:\s*if \(folder != root\) switchToFolder\(root\);/);

// A tap on the free background still closes it.
assert.match(slice(saver, 'void on_global_screensaver_clicked(lv_event_t* e) {', '\n}\n'),
  /if \(g_state && lv_event_get_target\(e\) == g_state->overlay\) \{\s*hide_image_screensaver\(\);/);
assert.match(saver, /lv_obj_add_event_cb\(st->overlay, on_global_screensaver_clicked,\s*LV_EVENT_CLICKED, nullptr\);/);

console.log('Screensaver swipe: Home opens it to the right, it goes back to Home to the left, a tap still closes it');
