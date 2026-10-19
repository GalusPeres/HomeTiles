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
assert.match(handlers, /for \(const char\* leaf : \{"\/stat\/media\/catalog", "\/stat\/media", "\/stat\/media\/thumb"\}\)/);
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
assert.match(fn('delete_list'), /lv_obj_add_flag\(ctx->list, LV_OBJ_FLAG_HIDDEN\);\s*if \(ctx->dead_lists\.empty\(\)\) lv_async_call\(delete_dead_lists, nullptr\);/);
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
console.log('Media popup b488: fixed library topic, list rebuild on change only, bars on "playing", state icon in the head');
