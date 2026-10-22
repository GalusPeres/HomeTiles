// Media popup fixes after the first hardware test of b487 (user 2026-10-19):
// - the library never loaded over the direct link: the panel subscribed
//   stat/media/catalog/+ and the link takes no wildcard subscription
//   (Bridge link_protocol.valid_topic); the pages now come on one fixed
//   topic;
// - the source list jerked while scrolling: every player update rebuilt it;
//   now only a change of its content does, never during a scroll;
// - the playing bars started with the play button's own immediate switch;
//   they move only while Home Assistant reports "playing";
// - the head showed the tile's fixed icon; it shows the icon's state form
//   (speaker-play, speaker-pause, speaker-off) like the draft.
// b489 (hardware test of b488): the lists are the Settings dropdown's options
// (text only, no thumbnails or round controls in the rows), the status floats
// over the list instead of pushing it, covers come in the hardware decoder's
// size again.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n?/g, '\n');
const popup = read('src/ui/popups/media/media_popup.cpp');
const fn = name => {
  const found = cppFunctionDefinitions(popup).filter(f => f.name === name);
  assert.equal(found.length, 1, name);
  return found[0].source;
};

// Library topics: fixed, also in the routing.
const handlers = read('src/network/mqtt/mqtt_handlers.cpp');
assert.match(handlers, /for \(const char\* leaf : \{"\/stat\/media\/catalog", "\/stat\/media"\}\)/);
assert.ok(!/stat\/media\/catalog\/\+/.test(handlers), 'no wildcard media subscription');
assert.ok(handlers.includes('const bool is_catalog = strcmp(rest, "/catalog") == 0;'));

// Lists: rebuilt on a change of their content only, a change during a
// scroll waits for its end (outside the list's own event).
const apply = fn('apply_init_to_context');
assert.match(apply, /list_signature\(ctx\) != ctx->list_signature\) \{\s*if \(lv_obj_is_scrolling\(ctx->list\)\) ctx->list_dirty = true;\s*else rebuild_list\(ctx, true\);/);
assert.ok(!/\) rebuild_list\(ctx, true\);\s*show_head/.test(apply), 'no rebuild on every update');
assert.match(fn('rebuild_list'), /ctx->list_signature = list_signature\(ctx\);/);
assert.match(fn('on_list_scroll_end'), /if \(ctx->list_dirty\) \{\s*lv_async_call\(rebuild_dirty_list, nullptr\);/);
const signature = fn('list_signature');
for (const part of ['x.state', 'x.features', 'x.source', 'x.sound_mode', '&x.sources', '&x.sound_modes', '&x.members',
  'candidate.entity_id', 'candidate.name', 'candidate.state']) assert.ok(signature.includes(part), `signature: ${part}`);
// A list taken down from its own row's click is deleted after the event, and
// the popup's deletion takes pending ones along.
// The menu (box and outside layer) goes after the event that closed it.
assert.match(fn('delete_list'), /for \(lv_obj_t\* obj : \{ctx->list_box, ctx->list_shield\}\) \{\s*if \(!obj\) continue;\s*lv_obj_add_flag\(obj, LV_OBJ_FLAG_HIDDEN\);\s*if \(ctx->dead_lists\.empty\(\)\) lv_async_call\(delete_dead_lists, nullptr\);/);
assert.match(fn('on_overlay_delete'), /ctx->dead_lists\.clear\(\);/);

// Bars: the reported state only, not the button's immediate switch.
assert.match(fn('bars_draw_cb'), /const bool playing = ctx && ctx->reported_playing;/);
assert.match(fn('media_motion_timer_cb'), /if \(ctx->bars && ctx->reported_playing\) lv_obj_invalidate\(ctx->bars\);/);
assert.match(apply, /ctx->reported_playing = init\.is_playing;/);
const command = fn('on_media_command');
assert.ok(!command.includes('reported_playing') && !command.includes('lv_obj_invalidate(ctx->bars)'),
  'the play button does not start the bars');
assert.match(read('src/tiles/runtime/tile_renderer.cpp'),
  /static bool media_is_playing_state\(const String& state\) \{[\s\S]*?return lower == "playing";/,
  'is_playing is the reported "playing"');

// Head icon: the state form of the tile's icon where it exists.
const headIcon = fn('head_icon');
assert.match(headIcon, /state\.equalsIgnoreCase\("playing"\)\s*\? "-play"/);
assert.match(headIcon, /state\.equalsIgnoreCase\("paused"\) \? "-pause"/);
assert.match(headIcon, /\(state\.equalsIgnoreCase\("off"\) \|\| state\.equalsIgnoreCase\("standby"\)\) \? "-off"/);
assert.match(headIcon, /getMdiCodepoint\(variant\) != mdiUnknownIconCodepoint\(\) \? getMdiChar\(variant\) : ctx->icon_char;/);
assert.match(headIcon, /if \(!ctx->icon_name\.length\(\) \|\| getMdiChar\(ctx->icon_name\) != ctx->icon_char\) return ctx->icon_char;/,
  'an icon the tile changed (a rule) stays as it is');
assert.match(fn('show_head'), /lv_label_set_text\(ctx->icon_label, head_icon\(ctx, extras_of\(ctx->entity_id\)\.state\)\.c_str\(\)\);/);
assert.match(fn('prepare_media_popup_open'),
  /lv_label_set_text\(ctx->icon_label, head_icon\(ctx, extras_of\(init\.entity_id\)\.state\)\.c_str\(\)\);/);
for (const name of ['speaker-play', 'speaker-pause', 'speaker-off', 'television-play', 'television-pause', 'television-off']) {
  assert.ok(read('src/tiles/icons/mdi_icons.cpp').includes(`{"${name}", 0x`), `${name} is an MDI icon`);
}
// b489: the Settings dropdown's option rows, nothing else in them (72 px
// high since 2026-10-19: more room between the entries).
const row = fn('list_row');
assert.match(row, /lv_obj_set_size\(row, LV_PCT\(100\), kListRowHeight - kOptionGap\);/);
assert.match(row, /settings_style::apply_radius\(row, settings_style::kGroupRadius - settings_style::kOptionInset\);/);
assert.match(row, /lv_obj_t\* label = add_text\(row, settings_style::row_font\(\), c\.text, ""\);/);
assert.ok(!/lv_image_create|clip_corner|round_button/.test(row), 'no picture or round control in a row');
assert.ok(!/clip_corner/.test(fn('append_library_rows')) && !/thumbnail/.test(fn('append_library_rows')),
  'the library asks for no pictures');
assert.ok(!popup.includes('mqttPublishMediaThumbnail'), 'no thumbnail requests');
// The status floats over the list, loading only after a moment.
assert.match(fn('build_library'), /lv_obj_add_flag\(ctx->library_status, LV_OBJ_FLAG_FLOATING\);/);
assert.match(fn('show_library_status'), /if \(since >= kLoadingDelayMs\) text = tr\.loading;/);
assert.match(fn('show_library_status'), /if \(empty \|\| since < kErrorShownMs\) text = tr\.media_library_failed;/);
// "Repeat one": the time starts again at the end without a report.
assert.match(fn('current_media_position'), /extras_of\(ctx->entity_id\)\.repeat\.equalsIgnoreCase\("one"\)\) \{\s*position = fmodf\(position, ctx->media_duration\);/);
console.log('Media popup b488/b489: fixed library topic, list rebuild on change only, bars on "playing", state icon in the head, dropdown rows, floating status');
// b490, kept in the menu of 2026-10-22: a scroll frame is one area (b489 on
// hardware: the list reached into rounded corners and every frame was cut
// into four areas). The rows' list is square, paints the menu's color itself
// and sits on the edge L inside the menu box, away from its rounded corners
// (simulator 2026-10-22: one area of about 258000 px per scroll frame).
const rebuild = fn('rebuild_list');
assert.match(rebuild, /lv_obj_set_pos\(ctx->list, box_pad, box_pad \+ head_height \+ g\);\s*lv_obj_set_size\(ctx->list, content_width, list_height\);\s*lv_obj_set_style_radius\(ctx->list, 0, 0\);/);
assert.match(fn('show_list_scrollbar_color'), /lv_obj_set_style_bg_color\(ctx->list, lv_color_hex\(ctx->colors\.card\), 0\);\s*lv_obj_set_style_bg_opa\(ctx->list, LV_OPA_COVER, 0\);/);
assert.match(rebuild, /const int box_offset = L \/ 2;\s*const int box_pad = L - box_offset;/);
console.log('Media popup b490: a scroll frame redraws only the list');
