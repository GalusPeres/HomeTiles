// The camera popup (mockup build/design-mockups/webcam-radar/radar-tall.html): the
// picture whole in its area, the bottom row with Play, the time bar with its times
// under it like the Media popup's and the "..." menu of the tile's other entries.
// The frames stay in the Bridge. No FPS line under a picture; "Live" or "Synced
// 15:00" in the head.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = (file) => fs.readFileSync(path.join(root, file), 'utf8').replaceAll('\r\n', '\n');
const popup = read('src/ui/popups/camera/camera_popup.cpp');
const stream = read('src/video/camera_stream.cpp');
const geometry = read('src/video/camera_geometry.h');
const mqtt = read('src/network/mqtt/mqtt_handlers.cpp');
const strings = read('src/core/i18n/i18n.cpp');
const header = read('src/core/i18n/i18n.h');

// No measured FPS on the screen: the log keeps it.
assert.doesNotMatch(header, /camera_fps_fmt/);
assert.doesNotMatch(strings, /%\.1f FPS|кадр\/с/);
assert.doesNotMatch(stream, /camera_fps_fmt/);
assert.match(stream, /\[CameraStream\] Present: %\.1f FPS/);
assert.doesNotMatch(popup, /kStatusTop/);

// The full screen stays a tap on the picture: no button (user 2026-11-05).
assert.doesNotMatch(popup, /full_button|"fullscreen"/);
assert.match(popup, /lv_obj_add_event_cb\(video, video_event_cb, LV_EVENT_CLICKED, ctx\)/);

// The head (user 2026-11-06): "Synced" and the newest picture's time in every language.
assert.match(header, /const char\* camera_live;[^\n]*\n\s*const char\* camera_synced_fmt;/);
assert.doesNotMatch(header, /camera_now|camera_minutes_ago_fmt/);
for (const text of ['Synchronisiert %s', 'Synced %s', 'Synchronisé %s', 'Zsynchronizowano %s', 'Synchronizováno %s',
                    'Sincronizado %s', 'Synchronizované %s', 'Синхронизировано %s']) {
  assert.ok(strings.includes(`"${text}",`), text);
}
assert.doesNotMatch(strings, /"As of %s"|"Stand %s"|%u min ago|vor %u Min"/);
// The type's name in every language, its id unchanged.
for (const text of ['Kamera / Bild', 'Camera / Picture', 'Caméra / Image', 'Kamera / Obraz', 'Kamera / Obrázek',
                    'Cámara / Imagen', 'Kamera / Obrázok', 'Камера / Изображение']) {
  assert.ok(strings.includes(`"${text}",`), text);
}

// The head's second line goes through the shell, like the Media popup's state; also
// with a single picture, and it stays while dragging: only the newest time counts.
assert.match(popup, /show_popup_shell\(g_camera_popup->overlay,[^;]*g_camera_popup->value_label\);/);
assert.match(popup, /show_popup_shell\(ctx->overlay,[^;]*ctx->value_label\);/);
assert.match(popup, /snprintf\(text, sizeof\(text\), camera_text\(\)\.camera_synced_fmt, when\)/);
assert.match(popup, /snprintf\(text, sizeof\(text\), "%s", camera_text\(\)\.camera_live\)/);
assert.match(popup, /show_head_text\(ctx, ctx->frame_count \? ctx->frame_times\[ctx->frame_count - 1\] : 0\);/);

// No words in the picture while it comes (Ready, Buffering, Preparing): only an
// error or a message that must stay, and none left over from the last opening.
assert.match(popup, /ctx->status = lv_label_create\(video\)/);
assert.match(popup, /set_shown\(g_camera_popup->status,\s*!bridge_quiet && \(g_camera_popup->status_sticky \|\| camera_stream_status_is_error\(\)\)\);/);
assert.match(popup, /g_camera_popup->status_sticky = false;\s*set_shown\(g_camera_popup->status, false\);/);
assert.match(popup, /ctx->status_sticky = false;\s*set_shown\(ctx->status, false\);/);
assert.doesNotMatch(popup, /placeholder|kStatusDelayMs|camera_preparing|camera_text\(\)\.camera_ready/);
assert.doesNotMatch(header, /camera_preparing/);
assert.match(popup, /\[Camera\] First frame shown %u ms after the opening/);
assert.match(stream, /bool camera_stream_status_is_error\(\) \{ return false; \}/);

// The picture's area: one edge under the head to one edge above the bottom row.
assert.match(geometry, /kAreaTop = media_popup_layout::kHeadTop \+ media_popup_layout::kDisc \+ media_popup_layout::kEdge;/);
assert.match(geometry, /kAreaBottom = media_popup_layout::kVolumeTop - media_popup_layout::kEdge;/);
const layouts = { V2: { edge: 28, head: 28, disc: 72, card: 800, button: 78, box: [752, 538], top: 128, row: 694 },
                  S3: { edge: 16, head: 16, disc: 48, card: 480, button: 52, box: [448, 316], top: 80, row: 412 } };
for (const [name, m] of Object.entries(layouts)) {
  assert.equal(m.head + m.disc + m.edge, m.top, name);
  assert.equal(m.card - m.edge - m.button, m.row, name);
  assert.equal(m.row - m.edge - m.top, m.box[1], name);
}
// The picture's middle on the card's middle, kept within its area (user
// 2026-11-19: without the bottom row it sat 33 to 50 px low).
assert.match(popup, /const int centred = \(popup_layout::kCardHeight - ctx->frame_h\) \/ 2;\s*const int top = std::max\(kAreaTop, std::min\(centred, kAreaTop \+ area - ctx->frame_h\)\);\s*lv_obj_align\(ctx->video, LV_ALIGN_TOP_MID, 0, top - popup_layout::kCardPad\);/);
for (const [name, m, frame] of [['V2', layouts.V2, 424], ['S3', layouts.S3, 252]]) {
  for (const row of [false, true]) {
    const area = row ? m.box[1] : m.card - m.edge - m.top;
    const top = Math.max(m.top, Math.min(Math.floor((m.card - frame) / 2), m.top + area - frame));
    assert.equal(top + frame / 2, m.card / 2, `${name} ${row ? 'with' : 'without'} the row: the picture on the card's middle`);
    assert.ok(top >= m.top && top + frame <= (row ? m.row - m.edge : m.card - m.edge), `${name}: within its area`);
  }
}
assert.match(popup, /const bool row = ctx->frame_count >= 2 \|\| ctx->entries\.size\(\) >= 2 \|\| is_radar\(ctx\);/);

// The stream takes the frame's own size within the box.
assert.match(mqtt, /\\"box\\":\[%u,%u\]/);
assert.match(popup, /camera_stream_start\(url\.c_str\(\), ctx->surface_color, full, ctx->frame_w, ctx->frame_h\)/);
assert.match(popup, /width <= camera_geometry::kWidth && width % 8 == 0 &&\s*height >= 16 && height <= camera_geometry::kBoxHeight && height % 2 == 0;/);
assert.match(stream, /picture_info\.width != g_frame_w \|\| picture_info\.height != g_frame_h/);
assert.match(stream, /area\.x1, area\.y1, g_frame_w, g_frame_h, frame_stride\(\),/);
// Buffers in this stream's size, no more (PSRAM is short on the S3), and the stream
// comes before the kept picture.
assert.match(stream, /static_cast<size_t>\(frame_stride\(\)\) \* \(\(g_frame_h \+ 15U\) & ~15U\) \* sizeof\(uint16_t\)/);
assert.doesNotMatch(stream, /kPixelBytes|kDecodedHeight/);
assert.match(stream, /\[CameraStream\] No frame buffer %u of %u bytes: psram free=%uKB largest=%uKB/);
assert.match(popup, /if \(!started && ctx->kept_dsc\) \{[\s\S]{0,160}hide_kept_picture\(ctx\);\s*started = camera_stream_start/);
// No 16:9 step for the radar; while switching the previous picture stays.
assert.match(popup, /ctx->frame_h = is_radar\(ctx\) \? camera_geometry::kBoxHeight : kVideoHeight;/);
assert.match(popup, /if \(!show_kept_picture\(ctx, ctx->entity_id, true\)\) show_kept_picture\(ctx, previous, false\);/);

// The bottom row: Play left, the bar from the Media popup's constants, the times
// under it at its ends, the menu in the Media popup's corner, the bar gives way.
for (const name of ['kSeekHeight', 'kSeekKnob', 'kEdge', 'kButton']) {
  assert.match(popup, new RegExp(`ml::${name}`), name);
}
assert.match(popup, /constexpr int kBarLeft = ml::kEdge \+ ml::kButton \+ kBarGap;/);
assert.match(popup, /constexpr int kBarGap = popup_layout::scale\(22\);/);
assert.match(popup, /const int knob_top = kButtonTop \+ \(ml::kButton - \(ml::kSeekKnob \+ kBarTimeGap \+ line\)\) \/ 2;/);
assert.match(popup, /lv_obj_set_pos\(ctx->time_shown, kBarLeft, time_top\)/);
assert.match(popup, /lv_obj_set_pos\(ctx->play_button, ml::kEdge, kButtonTop\)/);
assert.match(popup, /lv_obj_set_pos\(ctx->menu_button, popup_layout::kCardWidth - ml::kEdge - ml::kButton, kButtonTop\)/);
assert.match(popup, /control_button\(ctx->root, "dots-horizontal", &ctx->menu_label\)/);
assert.match(popup, /const int bar_right = popup_layout::kCardWidth - ml::kEdge - \(many \? ml::kButton \+ kBarGap : 0\);/);
assert.match(popup, /const lv_color_t fill = lv_color_hex\(tinted \? icon_color : ui_theme::icon\(\)\)/);
assert.match(popup, /popup_shell_control_fill\(ctx->surface_color, icon_color, control, control_opa, &tinted\)/);
// Play in the circle's color with the icon role.
assert.match(popup, /lv_obj_set_style_bg_color\(ctx->play_button, control, 0\);/);
assert.match(popup, /lv_obj_set_style_text_color\(ctx->play_label, lv_color_hex\(ui_theme::icon\(\)\), 0\);/);

// The menu as the Media popup's: only as high as its entries, a selected row in the
// control fill, the picture stands under it (the P4 writes frames straight to the
// panel), a tap beside it closes it; rows are never deleted from their own click.
assert.match(popup, /const int top = kButtonTop - gap - list_height - gap - ml::kButton - pad;/);
assert.match(popup, /constexpr size_t kMenuRowsShown = 5;/);
assert.match(popup, /i == ctx->entry_index \? control_opa : LV_OPA_TRANSP/);
assert.match(popup, /if \(g_camera_popup->menu_open\) \{\s*\} else/);
assert.doesNotMatch(popup.slice(popup.indexOf('static void menu_row_event_cb'), popup.indexOf('static void menu_shield_event_cb')), /lv_obj_clean|lv_obj_delete/);
assert.match(popup, /lv_label_set_text\(ctx->menu_title, kind == camera_entries::kWebcam\s*\? text\.camera_source_webcam/);
// Switching: the other entity's stream, the old one closed, the tile told.
const entry = popup.slice(popup.indexOf('static void open_entry'), popup.indexOf('static void menu_row_event_cb'));
assert.match(entry, /mqttPublishCameraCommand\(ctx->entity_id\.c_str\(\), "close"\)/);
assert.match(entry, /camera_stream_stop\(\);/);
assert.match(entry, /show_kept_picture\(ctx, ctx->entity_id, true\)/);
assert.match(entry, /open_popup_stream\(ctx\);/);
assert.match(entry, /if \(ctx->remember_key\) camera_tile_entry_chosen\(ctx->remember_key, index\);/);

// The last picture: per entity, a few JPEGs, shown at once, gone with the first
// frame, kept only for a still camera on its newest picture and never per frame.
assert.match(popup, /constexpr size_t kKeptPictures = 4;/);
assert.match(popup, /ctx->keeping = !full && !ctx->live_kind;/);
assert.match(popup, /camera_stream_keep_jpeg\(g_camera_popup->keeping && !g_camera_popup->full && !g_camera_popup->playing &&/);
assert.match(stream, /constexpr uint32_t kKeepEveryMs = 5000;/);
assert.match(stream, /if \(g_keep_copied && now - g_keep_copied_ms < kKeepEveryMs\) return;/);

// The panel keeps no history: it knows the times and asks for one.
assert.match(popup, /uint32_t frame_times\[kMaxFrames\]/);
assert.match(popup, /constexpr uint8_t kMaxFrames = 150;/);
assert.doesNotMatch(popup, /frame_cache/);
assert.match(popup, /mqttPublishCameraFrame\(ctx->entity_id\.c_str\(\), newest \? 0 : ctx->frame_times\[ctx->shown_frame\]\)/);
assert.match(mqtt, /\\"command\\":\\"frame\\",\\"time\\":%lu/);
// A long history comes as minutes after the first (camera_sources.py frames_payload).
assert.match(popup, /doc\["frames_min"\]/);
assert.match(popup, /base \+ item\.as<uint32_t>\(\) \* 60/);

// At most one request per kFrameSendMs: while dragging only the newest position.
assert.match(popup, /if \(!ctx->frame_dirty \|\| now - ctx->frame_sent_ms < kFrameSendMs\) return;/);

// A value from outside never moves the knob under the finger; it follows on release.
assert.match(popup, /if \(!lv_slider_is_dragged\(ctx->bar\)\)/);
assert.match(popup, /code != LV_EVENT_RELEASED && code != LV_EVENT_PRESS_LOST/);

// Dragging ends Play; Play moves at once and runs in a loop, staying a little
// longer on the newest picture; a day of pictures goes in larger steps.
const event = popup.slice(popup.indexOf('static void bar_event_cb'), popup.indexOf('static void play_event_cb'));
assert.match(event, /set_playing\(ctx, false\)/);
const play = popup.slice(popup.indexOf('static void play_event_cb'), popup.indexOf('static void full_touch_event_cb'));
assert.match(play, /set_playing\(ctx, true\);\s*play_step\(ctx\);/);
const step = popup.slice(popup.indexOf('static void play_step'), popup.indexOf('static void process_time_bar'));
assert.match(step, /if \(ctx->shown_frame >= last\) ctx->shown_frame = 0;/);
assert.match(step, /last - ctx->shown_frame < step \? last : ctx->shown_frame \+ step/);
assert.match(step, /ctx->shown_frame >= last \? kPlayHoldMs : long_history \? kPlayLongStepMs : kPlayStepMs/);

// The full screen has no controls: a history plays by itself there, both when
// it opens and when the frames arrive later; leaving it ends Play.
assert.equal(popup.match(/if \((g_camera_popup->)?full && g_camera_popup->frame_count >= 2\) set_playing\(g_camera_popup, true\);/g).length, 2);
assert.equal(popup.match(/ctx->full = false;\n  set_playing\(ctx, false\);/g).length, 2);

// A picked frame survives a new stream, and the frames are kept by their time.
assert.match(popup, /if \(g_camera_popup->shown_frame \+ 1 < g_camera_popup->frame_count\) g_camera_popup->frame_dirty = true;/);
assert.match(popup, /if \(ctx->frame_times\[i\] <= shown_time\) ctx->shown_frame = i;/);

// An older Bridge sends no times: no bar, no Play.
assert.match(popup, /const bool shown = ctx->frame_count >= 2;/);

// The Bridge's new frames are not an answer: a pending request keeps its timeout.
const handler = popup.slice(popup.indexOf('void camera_popup_handle_mqtt_status'));
assert.ok(handler.indexOf('"frames") == 0') < handler.indexOf('waiting_for_bridge = false'));

// The bottom row stays while switching and is there at once when opening (user
// 2026-11-07): the times are kept with the picture, every radar place has the same.
assert.match(popup, /std::vector<uint32_t> times;/);
assert.match(popup, /if \(radar_before && is_radar\(ctx\) && ctx->frame_count\) \{\s*ctx->shown_frame = ctx->frame_count - 1;/);
assert.match(popup, /return is_radar\(ctx\) && ctx->frame_count >= 2 && doc\["frames"\]\.size\(\) \+ doc\["frames_min"\]\.size\(\) < 2;/);
// A small gap between the menu's rows, its shadow as pictures like the Media popup's.
assert.match(popup, /lv_obj_set_style_pad_row\(ctx->menu_list, kMenuRowGap, 0\);/);
assert.match(popup, /lv_snapshot_take\(probe, LV_COLOR_FORMAT_A8\)/);
assert.doesNotMatch(popup, /apply_card_shadow\(ctx->menu_box\)/);
assert.match(popup, /free_menu_shadow\(g_camera_popup, true\);/);
assert.match(popup, /free_menu_shadow\(ctx, false\);/);
// No whole picture drawn again when the first frame already lies on the panel.
assert.match(popup, /if \(!no_frame\) hide_kept_picture\(g_camera_popup, false\);/);
assert.match(stream, /The frame already lies on the panel[\s\S]{0,400}lv_display_enable_invalidation\(display, false\);[\s\S]{0,120}lv_obj_clear_flag\(image, LV_OBJ_FLAG_HIDDEN\);/);

// One message at a time; the Bridge's own camera that is just coming gets asked again.
assert.match(popup, /const bool own_source = g_camera_popup->entity_id\.startsWith\("camera\.ht_"\);/);
assert.match(popup, /if \(g_camera_popup->source_retries\+\+ == 0\) return;/);

console.log('Camera popup: PASS');
