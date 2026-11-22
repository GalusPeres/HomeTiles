// Screensaver tiles with the automatic icon (Home Assistant's icon for their
// entity) follow late or changed icons like the pages do: a screensaver
// opened right after a start, before the Bridge sent its icons, showed those
// tiles without icon and circle until it was rebuilt (user 2026-10-31). Only
// a flag crosses tasks; the screensaver's own LVGL timer rebuilds the tiles.
import assert from 'node:assert/strict';

import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';
import {readRepoFile} from '../../lib/admin-source.mjs';

const read = (file) => readRepoFile(file).replace(/\r\n/g, '\n');
const screensaver = read('src/ui/screensaver/image_screensaver.cpp');
const tiles = read('src/ui/tabs/tiles/tab_tiles_unified.cpp');
const fn = (source, name) => {
  const found = cppFunctionDefinitions(source).find((f) => f.name === name);
  assert.ok(found, name);
  return found.source;
};

assert.ok(fn(tiles, 'tiles_request_icon_refresh').includes('image_screensaver_icons_changed();'),
  'every icon refresh also reaches the screensaver');
assert.equal(fn(screensaver, 'image_screensaver_icons_changed').replace(/\s+/g, ' ').trim(),
  'void image_screensaver_icons_changed() { g_live_icon_refresh_requested = true; }',
  'the hook only sets a flag (it runs on the MQTT task)');
assert.match(fn(screensaver, 'follows_entity_icon'),
  /tileTypeRefreshesEntityIcon\(static_cast<TileType>\(tile\.type\)\) && !isMdiIconDisabled\(tile\.icon_name\) &&\s*!normalizeMdiIconName\(tile\.icon_name\)\.length\(\)/,
  'only tiles without an icon of their own follow the entity icon');
assert.ok(fn(screensaver, 'update_slot_grid').includes('!(icons_changed && follows_entity_icon(after))'),
  'with new icons those tiles are built again');
assert.match(fn(screensaver, 'global_screensaver_timer_cb'),
  /if \(g_live_icon_refresh_requested\) \{\s*g_live_icon_refresh_requested = false;\s*if \(!update_slot_grid\(st, true\)\) \{\s*rebuild_slot_grid\(st\);\s*refresh_slot_values\(st\);\s*\}/,
  'the screensaver timer rebuilds them in the LVGL context');
for (const name of ['show_image_screensaver', 'hide_image_screensaver']) {
  assert.ok(fn(screensaver, name).includes('g_live_icon_refresh_requested = false;'), `${name} drops an old request`);
}
console.log('Screensaver entity icons: late Home Assistant icons reach the tiles with the automatic icon');
