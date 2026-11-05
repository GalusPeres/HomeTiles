// The camera popup with a history (the Bridge's rain radar, Bridge rain_radar.py):
// a time bar with Play and dragging like the Media popup's, the frames stay in
// the Bridge. No FPS line under a picture; "Live" or the picture's time in the head.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = (file) => fs.readFileSync(path.join(root, file), 'utf8').replaceAll('\r\n', '\n');
const popup = read('src/ui/popups/camera/camera_popup.cpp');
const stream = read('src/video/camera_stream.cpp');
const mqtt = read('src/network/mqtt/mqtt_handlers.cpp');
const strings = read('src/core/i18n/i18n.cpp');
const header = read('src/core/i18n/i18n.h');

// No measured FPS on the screen: the log keeps it.
assert.doesNotMatch(header, /camera_fps_fmt/);
assert.doesNotMatch(strings, /%\.1f FPS|кадр\/с/);
assert.doesNotMatch(stream, /camera_fps_fmt/);
assert.match(stream, /\[CameraStream\] Present: %\.1f FPS/);
assert.doesNotMatch(popup, /kStatusTop/);

// Every language has both new texts, in the place of the old one.
assert.match(header, /const char\* camera_live;[^\n]*\n\s*const char\* camera_picture_time_fmt;/);
for (const [live, time] of [['Live', 'Stand %s'], ['Live', 'As of %s'], ['En direct', 'À %s'], ['Na żywo', 'Stan z %s'],
                            ['Živě', 'Stav k %s'], ['En directo', 'A las %s'], ['Naživo', 'Stav k %s'], ['В эфире', 'На %s']]) {
  assert.ok(strings.includes(`"${live}",\n    "${time}",`), `${live} / ${time}`);
}

// The head's second line goes through the shell, like the Media popup's state.
assert.match(popup, /show_popup_shell\(g_camera_popup->overlay,[^;]*g_camera_popup->value_label\);/);
assert.match(popup, /show_popup_shell\(ctx->overlay,[^;]*ctx->value_label\);/);
assert.match(popup, /snprintf\(text, sizeof\(text\), camera_text\(\)\.camera_picture_time_fmt, when\)/);
assert.match(popup, /snprintf\(text, sizeof\(text\), "%s", camera_text\(\)\.camera_live\)/);

// The status stays in the picture's area: until the first frame, then only an
// error or a message that must stay.
assert.match(popup, /ctx->status = lv_label_create\(video\)/);
assert.match(popup, /set_shown\(g_camera_popup->status, lv_obj_has_flag\(g_camera_popup->image, LV_OBJ_FLAG_HIDDEN\) \|\|\s*g_camera_popup->status_sticky \|\| camera_stream_status_is_error\(\)\)/);
assert.match(stream, /bool camera_stream_status_is_error\(\) \{ return false; \}/);

// The bar as in the Media popup: its constants, the times under it on the edges.
for (const name of ['kSeekHeight', 'kSeekKnob', 'kTimeGap', 'kEdge', 'kButton']) {
  assert.match(popup, new RegExp(`ml::${name}`), name);
}
assert.match(popup, /const int time_top = mid \+ ml::kSeekHeight \/ 2 \+ ml::kTimeGap;/);
assert.match(popup, /lv_obj_set_pos\(ctx->time_shown, ml::kEdge, time_top\)/);
assert.match(popup, /lv_obj_align\(ctx->time_newest, LV_ALIGN_TOP_RIGHT, -ml::kEdge, time_top\)/);
assert.match(popup, /const lv_color_t fill = lv_color_hex\(tinted \? icon_color : ui_theme::icon\(\)\)/);
assert.match(popup, /popup_shell_control_fill\(ctx->surface_color, icon_color, control, control_opa, &tinted\)/);

// The same space from the picture to the knob as from the digits to the buttons.
const line = { V2: 24, S3: 17 };
const ml = { V2: { top: 128, h: 424, button: 694, knob: 26, seek: 10, gap: 14 },
             S3: { top: 80, h: 252, button: 412, knob: 17, seek: 7, gap: 9 } };
for (const name of ['V2', 'S3']) {
  const m = ml[name];
  const mid = Math.trunc((m.button + m.top + m.h + Math.trunc(m.knob / 2) - Math.trunc(m.seek / 2) - m.gap -
                          Math.trunc(line[name] * 4 / 5)) / 2);
  const above = mid - Math.trunc(m.knob / 2) - (m.top + m.h);
  const below = m.button - (mid + Math.trunc(m.seek / 2) + m.gap + Math.trunc(line[name] * 4 / 5));
  assert.ok(Math.abs(above - below) <= 2, `${name}: ${above} above, ${below} below`);
  assert.ok(below > 0, `${name}: the times stay clear of the buttons`);
}
assert.match(popup, /const int mid = \(kButtonTop \+ kVideoTopWithBar \+ kVideoHeight \+ ml::kSeekKnob \/ 2 - ml::kSeekHeight \/ 2 -\s*ml::kTimeGap - line \* 4 \/ 5\) \/ 2;/);

// The panel keeps no frames: it knows the times and asks for one.
assert.match(popup, /uint32_t frame_times\[kMaxFrames\]/);
assert.doesNotMatch(popup, /frame_jpeg|frame_cache/);
assert.match(popup, /mqttPublishCameraFrame\(ctx->entity_id\.c_str\(\), newest \? 0 : ctx->frame_times\[ctx->shown_frame\]\)/);
assert.match(mqtt, /\\"command\\":\\"frame\\",\\"time\\":%lu/);

// At most one request per kFrameSendMs: while dragging only the newest position.
assert.match(popup, /if \(!ctx->frame_dirty \|\| now - ctx->frame_sent_ms < kFrameSendMs\) return;/);

// A value from outside never moves the knob under the finger; it follows on release.
assert.match(popup, /if \(!lv_slider_is_dragged\(ctx->bar\)\)/);
assert.match(popup, /code != LV_EVENT_RELEASED && code != LV_EVENT_PRESS_LOST/);

// Dragging ends Play; Play from the newest starts at the oldest and stops at the newest.
const event = popup.slice(popup.indexOf('static void bar_event_cb'), popup.indexOf('static void play_event_cb'));
assert.match(event, /set_playing\(ctx, false\)/);
const play = popup.slice(popup.indexOf('static void play_event_cb'), popup.indexOf('static void full_touch_event_cb'));
assert.match(play, /ctx->shown_frame = 0;/);
const step = popup.slice(popup.indexOf('static void process_time_bar'), popup.indexOf('static void open_popup_stream'));
assert.match(step, /if \(ctx->shown_frame \+ 1 >= ctx->frame_count\) set_playing\(ctx, false\)/);

// Full screen ends Play on both ways of showing it.
for (const name of ['enter_soft_full_screen', 'enter_full_screen']) {
  const body = popup.slice(popup.indexOf(`static void ${name}() {`));
  assert.match(body.slice(0, 320), /set_playing\(ctx, false\)/, name);
}

// A picked frame survives a new stream, and the frames are kept by their time.
assert.match(popup, /if \(g_camera_popup->shown_frame \+ 1 < g_camera_popup->frame_count\) g_camera_popup->frame_dirty = true;/);
assert.match(popup, /if \(ctx->frame_times\[i\] <= shown_time\) ctx->shown_frame = i;/);

// An older Bridge sends no times: no bar, no Play, the plain layout.
assert.match(popup, /const bool shown = ctx->frame_count >= 2;/);
assert.match(popup, /if \(has_history != g_camera_popup->with_bar\) apply_layout\(g_camera_popup, has_history\);/);

// The Bridge's new frames are not an answer: a pending request keeps its timeout.
const handler = popup.slice(popup.indexOf('void camera_popup_handle_mqtt_status'));
assert.ok(handler.indexOf('"frames") == 0') < handler.indexOf('waiting_for_bridge = false'));

console.log('Camera time bar: PASS');
