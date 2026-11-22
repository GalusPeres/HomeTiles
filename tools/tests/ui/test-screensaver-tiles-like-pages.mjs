// Screensaver tiles behave like their page's (user 2026-10-31): a tap opens
// the tile's popup (or toggles, by the tile's press setting) over the
// screensaver, Sensor tiles show their graph, gauge and looks, Energy tiles
// their bars, and the editor offers the same choices. While such a popup is
// open the screensaver pauses its slides, the pixel shift and new Home
// Assistant pictures, so no full-screen frame is composed under it.
// Checks every tile type's renderer and editor, not a hand-picked few.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';

import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';
import {readRepoFile} from '../../lib/admin-source.mjs';

const root = path.resolve(import.meta.dirname, '../../..');
const read = (file) => readRepoFile(file).replace(/\r\n/g, '\n');
const fn = (source, name) => {
  const found = cppFunctionDefinitions(source).find((f) => f.name === name);
  assert.ok(found, name);
  return found.source;
};

// ---- Every tile type --------------------------------------------------------
// Kept on purpose: the Camera popup switches the display's double buffer the
// screensaver's slides use, the camera picture stays off the screensaver, and
// the Power flow draws no frame lines over a picture.
const allowed = {
  'src/types/camera/renderer.cpp': 2,
  'src/types/power_flow/renderer.cpp': 1,
};
const typeFiles = [];
for (const dir of fs.readdirSync(path.join(root, 'src/types'))) {
  const full = path.join(root, 'src/types', dir);
  if (!fs.statSync(full).isDirectory()) continue;
  for (const file of fs.readdirSync(full)) {
    if (file.endsWith('.cpp')) typeFiles.push(`src/types/${dir}/${file}`);
  }
}
assert.ok(typeFiles.length > 30, 'the type folders were read');
for (const file of typeFiles) {
  const source = read(file);
  const gates = (source.match(/grid_type != GridType::SCREENSAVER/g) || []).length;
  assert.equal(gates, allowed[file] || 0, `${file}: a screensaver tile reacts like the page's (${gates} exclusions)`);
  assert.doesNotMatch(source, /tab_id != "screensaver"/, `${file}: the editor offers the page's choices on the screensaver`);
}
// Saved and loaded as chosen (user 2026-10-31: a screensaver Sensor set to
// its graph stayed a value; the save and the stored grid's normalizing reset
// the display to 0 from the time history reached no screensaver tile).
assert.doesNotMatch(read('src/web/server/handlers/web_admin_tiles.cpp'), /screensaver_grid && tile\.type == TILE_SENSOR/,
  'a screensaver Sensor keeps its display when saved');
assert.doesNotMatch(fn(read('src/ui/screensaver/screensaver_config.cpp'), 'ScreensaverConfigStore::normalizeTileGrid'), /sensor_display_mode = 0/,
  'a screensaver Sensor keeps its display when loaded');
const importSource = read('src/web/admin/tiles/import-export.js');
assert.doesNotMatch(importSource, /screensaverEnergy|isScreensaverTile\s*\?\s*0/, 'an import keeps a screensaver tile\'s display');

// ---- The popup over the screensaver ------------------------------------------
const shell = read('src/ui/popups/popup_shell.cpp');
assert.ok(fn(shell, 'show_popup_shell').includes(
  'lv_obj_set_parent(shell.overlay, g_screensaver_open ? lv_layer_top() : lv_screen_active());'),
'a popup opened over the screensaver lies on the top layer above its overlay');
assert.match(fn(shell, 'popup_shell_over_screensaver'), /shell\.active && shell\.overlay && lv_obj_get_parent\(shell\.overlay\) == lv_layer_top\(\)/,
  'only a popup over the screensaver counts, not a page popup left open under it');
const screensaver = read('src/ui/screensaver/image_screensaver.cpp');
assert.ok(fn(screensaver, 'show_image_screensaver').includes('popup_shell_screensaver_open(true);'), 'opening tells the shell');
assert.ok(fn(screensaver, 'hide_image_screensaver').includes('popup_shell_screensaver_open(false);'), 'closing tells the shell');
assert.ok(fn(screensaver, 'on_global_overlay_delete').includes('popup_shell_screensaver_open(false);'),
  'an overlay LVGL deleted itself tells the shell too');
const timer = fn(screensaver, 'global_screensaver_timer_cb');
const pause = timer.indexOf('if (popup_shell_over_screensaver()) return;');
assert.ok(pause > 0, 'an open popup pauses the screensaver');
assert.ok(pause > timer.indexOf('refresh_slot_values(st);'), 'the tiles under it keep their values');
for (const later of ['g_ha_picture_arrived = false;', 'pixel_shift_due(', 'next_enabled_wallpaper(']) {
  assert.ok(timer.indexOf(later) > pause, `${later} waits while the popup is open`);
}

// ---- Graphs on the screensaver ---------------------------------------------------
const renderer = read('src/tiles/runtime/tile_renderer.cpp');
assert.match(fn(renderer, 'apply_tile_graph_history'),
  /\{&tileConfig\.getActiveGrid\(\), g_tab0_sensors\},\s*\{image_screensaver_tile_grid\(\), g_screensaver_sensors\}/,
  'a day answer reaches the graph tiles of the page and of an open screensaver');
assert.match(fn(renderer, 'tile_graph_slot'),
  /page \? &tileConfig\.getActiveGrid\(\) : image_screensaver_tile_grid\(\)/, 'the graph round asks for both');
assert.match(fn(screensaver, 'image_screensaver_tile_grid'), /return g_state && g_state->slot_grid \? g_state->shown_grid : nullptr;/,
  'the screensaver\'s grid only while it shows');
// A Media popup opened from the screensaver takes that tile's cover and its
// later song, state and cover updates (user 2026-11-01, S3 b644: no cover when
// the player sits only on the screensaver).
const findCover = fn(renderer, 'tile_renderer_find_media_cover');
assert.match(findCover, /GridType::SCREENSAVER\}/, 'the cover lookup includes the screensaver grid');
assert.match(findCover, /g_screensaver_media\}/, 'and its media widgets');
assert.doesNotMatch(fn(renderer, 'update_media_popup_from_widgets'), /GridType::SCREENSAVER\) return;/,
  'the screensaver tile updates its open popup like a page tile');
console.log('Screensaver tiles: popups, graphs and choices like the pages; slides pause under a popup; Media popup cover');
