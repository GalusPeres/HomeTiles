// A page turn draws first and takes changed icons right after its frame
// (user 2026-10-28: page turns too slow; b586 log "folder-icons refreshed
// folder=1 ... in 83 ms" before the page was drawn, after every icon update
// from Home Assistant). Only a grid built before Home Assistant's first
// icons refreshes before its first frame, so it never shows without them.
import assert from 'node:assert/strict';
import {readRepoFile} from '../../lib/admin-source.mjs';

const tiles = readRepoFile('src/ui/tabs/tiles/tab_tiles_unified.cpp').replace(/\r\n?/g, '\n');
const slice = (start, end) => {
  const from = tiles.indexOf(start);
  assert.ok(from >= 0, 'missing ' + start);
  return tiles.slice(from, tiles.indexOf(end, from + start.length) + end.length);
};

const stale = slice('static void refresh_active_folder_icons_if_stale(', '\n}\n');
assert.match(stale, /if \(g_tiles_first_icon_generation && entry\.icon_generation >= g_tiles_first_icon_generation\) \{\s*g_tiles_icon_refresh_requested = true;\s*return;\s*\}/,
  'a grid with Home Assistant icons defers the refresh to the loop');
assert.ok(stale.indexOf('g_tiles_icon_refresh_requested = true;') < stale.indexOf('tiles_refresh_icons_for_grid(GridType::TAB0);'),
  'only a grid without them refreshes before its frame');

const request = slice('void tiles_request_icon_refresh() {', '\n}\n');
assert.match(request, /if \(!g_tiles_first_icon_generation\) g_tiles_first_icon_generation = g_tiles_icon_generation;/,
  'the first icons are remembered');

// b589 rebuilt the hidden grids made before the first icons in the
// background, together with Home Assistant's start data; about 60 s after
// boot the V2's PPA rotation hung (blue screen, restart, user 2026-10-28).
// Removed: no background rebuild after the first icons.
assert.doesNotMatch(tiles, /mark_grids_without_icons|g_tiles_first_icons_pending/);

// The loop refreshes the shown grid after the frame and logs it.
const loop = slice('if (g_tiles_icon_refresh_requested && !did_reload) {', '\n  }\n');
assert.match(loop, /g_active_cache->icon_generation == g_tiles_icon_generation\) \{\s*continue;/);
assert.ok(loop.includes('"[Tiles] folder-icons refreshed after the frame folder=%u generation=%lu in %lu ms\\n"'));

// The page turn itself is still drawn by the folder switch's own refresh.
const pending = slice('void tiles_process_pending_folder_switch() {', '\n}\n');
assert.ok(pending.indexOf('tiles_process_reload_requests();') < pending.indexOf('lv_refr_now(display);'));
console.log('Page turn icon refresh: drawn first, changed icons right after the frame');
