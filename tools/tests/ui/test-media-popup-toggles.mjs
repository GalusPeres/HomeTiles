// Shuffle and repeat like Spotify (user 2026-10-23, mockup
// build/design-mockups/media-popup-simple/toggles.html): on in the icon's
// color with a dot under it, off only the icon in the secondary color; no
// circle at rest (b527 filled an active one), a press shows the control fill
// like every button.
import assert from 'node:assert/strict';
import {readRepoFile} from '../../lib/admin-source.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const popup = readRepoFile('src/ui/popups/media/media_popup.cpp');
const fn = name => {
  const found = cppFunctionDefinitions(popup).filter(f => f.name === name);
  assert.equal(found.length, 1, name);
  return found[0].source;
};

const toggle = fn('style_toggle');
assert.match(toggle, /lv_obj_set_style_text_color\(label, lv_color_hex\(on \? c\.icon : c\.secondary\), 0\);/);
assert.match(toggle, /lv_obj_t\* dot = lv_obj_get_child\(lv_obj_get_parent\(label\), 1\);\s*if \(dot\) set_shown\(dot, on\);/);
assert.ok(!/set_bg/.test(toggle), 'no circle at rest, the press fill stays round_button\'s');

const dot = fn('add_toggle_dot');
assert.match(dot, /lv_obj_t\* dot = plain\(btn\);/, 'not clickable: the press goes to the button');
assert.match(dot, /lv_obj_set_size\(dot, ml::kToggleDot, ml::kToggleDot\);/);
assert.match(dot, /lv_obj_set_style_bg_color\(dot, lv_color_hex\(c\.icon\), 0\);/);
assert.match(dot, /lv_obj_align\(dot, LV_ALIGN_CENTER, 0, ml::kToggleDotY\);/);
assert.match(dot, /lv_obj_add_flag\(dot, LV_OBJ_FLAG_HIDDEN\);/);

const player = fn('build_player');
assert.match(player, /add_toggle_dot\(control_button\(ctx, cx\(0\), ml::kTransportTop, "shuffle-variant", MediaAction::Shuffle,\s*&ctx->shuffle_label\), c\);/);
assert.match(player, /add_toggle_dot\(control_button\(ctx, cx\(4\), ml::kTransportTop, "repeat", MediaAction::Repeat, &ctx->repeat_label\), c\);/);

const layout = readRepoFile('src/ui/popups/media/media_popup_layout.h');
assert.match(layout, /constexpr int kToggleDot = popup_layout::scale\(6\);/);
assert.match(layout, /constexpr int kToggleDotY = popup_layout::scale\(28\);/);
console.log('Media popup toggles: icon color and a dot when on, no circle at rest');
