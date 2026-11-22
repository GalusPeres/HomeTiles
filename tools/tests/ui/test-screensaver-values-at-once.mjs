// A new state reaches the screensaver's tiles at once: they read the entity
// cache only in the screensaver's once-a-second refresh, so a Media tile's
// next track (its title and the key its new cover is shown under) came up to
// a second after the popup's (user 2026-11-02, V2 b653: the cover each time
// "adopted from neighboring tile" one timer tick after the page's tile had
// it). The once-a-second refresh stays for the other ways into the cache.
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const screensaver = readRepoFile('src/ui/screensaver/image_screensaver.cpp').replace(/\r\n/g, '\n');
const tiles = readRepoFile('src/ui/tabs/tiles/tab_tiles_unified.cpp').replace(/\r\n/g, '\n');
const header = readRepoFile('src/ui/screensaver/image_screensaver.h').replace(/\r\n/g, '\n');
const fn = (source, name) => cppFunctionDefinitions(source).find(f => f.name === name)?.source ?? '';

assert.match(header, /void image_screensaver_entity_changed\(const char\* entity_id\);/);

// Every new state of an entity tells the screensaver once (the first grid's
// call caches it).
const update = fn(tiles, 'tiles_update_sensor_by_entity');
assert.match(update, /const bool changed = cache_entity_payload\(entity_id, value\);\n[^\n]*\n  if \(changed\) image_screensaver_entity_changed\(entity_id\);/,
  'a changed state tells the screensaver');

// Only an entity a shown tile uses (its own or its rule's) wakes the timer,
// the established way (image_screensaver_picture_arrived).
const changed = fn(screensaver, 'image_screensaver_entity_changed');
assert.match(changed, /if \(!st \|\| !st->timer \|\| !entity_id \|\| !entity_id\[0\]\) return;/, 'only an open screensaver');
assert.match(changed, /tile\.sensor_entity\.equalsIgnoreCase\(entity_id\)/, 'the tile\'s own entity');
assert.match(changed, /tile_icon_source::rule_entity\(tile\)\.equalsIgnoreCase\(entity_id\)/, 'a rule\'s entity');
assert.match(changed, /g_slot_values_changed = true;\n\s*lv_timer_ready\(st->timer\);/, 'the timer runs at once');

// The timer refreshes at once on the flag, else once a second; the frosted
// time stays one line a second.
const timer = fn(screensaver, 'global_screensaver_timer_cb');
assert.match(timer, /if \(g_slot_values_changed \|\| static_cast<int32_t>\(now - st->next_slot_refresh_ms\) >= 0\) \{\n    g_slot_values_changed = false;\n    refresh_slot_values\(st\);\n    st->next_slot_refresh_ms = now \+ 1000U;/,
  'refreshed at once on a new state, else once a second');
assert.match(timer, /frosted\.draws && static_cast<uint32_t>\(millis\(\) - g_frosted_logged_ms\) >= 1000U/,
  'the frosted log stays once a second when the timer runs early');
// The values refresh before a popup over the screensaver returns early.
assert.ok(timer.indexOf('refresh_slot_values(st);\n    st->next_slot_refresh_ms') < timer.indexOf('if (popup_shell_over_screensaver()) return;'),
  'values update under a popup too');

console.log('Screensaver values at once: a new state reaches its tiles without waiting for the once-a-second refresh');
