// A page turn to a cached folder re-rendered its weather tile completely on
// every turn (V2 b597: Home took 42 ms from the tap to its first frame). The
// reset of the weather hash existed because a hidden folder's build applied
// the weather with the shown folder's tile at the same index (wrong forecast
// count). The tile now comes from the grid being built, and the hash holds
// everything the drawing depends on (payload, sensors, today's date, day or
// night), so a restored folder keeps its weather as drawn.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n?/g, '\n');
const fn = (source, name) => {
  const found = cppFunctionDefinitions(source).find(item => item.name === name);
  assert.ok(found, name);
  return found.source;
};

const runtime = read('src/tiles/runtime/tile_renderer.cpp');
const update = fn(runtime, 'update_weather_tile_state');
assert.ok(update.includes('const Tile* tile_ptr = tile_renderer_get_tile_config(grid_type, grid_index);'),
  'the tile of the grid being built, not the shown one');
assert.ok(!update.includes('tileConfig.getActiveGrid()'), 'never the shown folder\'s tile');
const hashAt = update.indexOf('if (widgets.last_payload_hash == payload_hash)');
const head = update.slice(0, hashAt);
assert.ok(head.includes('get_local_today_date(today_date)') && head.includes('weather_icons::parse_sun(payload, sun);') &&
  head.includes('weather_icons::night_now(sun)') && head.includes('fnv1a_hash(today_date.c_str())') &&
  head.includes('night ? 0x9E3779B9U : 0U'), 'date and day or night are part of the hash');
assert.equal((update.match(/get_local_today_date\(/g) || []).length, 1, 'the date is read once');
assert.equal((update.match(/weather_icons::parse_sun\(/g) || []).length, 1, 'the sun times are read once');

// The hidden build applies its weather while its own grid is the build grid.
const tiles = read('src/ui/tabs/tiles/tab_tiles_unified.cpp');
const build = fn(tiles, 'build_folder_cache_entry');
const setAt = build.indexOf('tile_renderer_set_build_grid(&config);');
assert.ok(setAt >= 0 && setAt < build.indexOf('process_weather_update_queue();') &&
  build.indexOf('process_weather_update_queue();') < build.indexOf('tile_renderer_set_build_grid(nullptr);'));
assert.ok(fn(runtime, 'tile_renderer_get_tile_config').includes('if (g_build_grid) return &g_build_grid->tiles[index];'));

// No turn drops the weather hashes any more.
for (const file of ['src/tiles/runtime/tile_renderer.cpp', 'src/tiles/runtime/tile_renderer.h',
  'src/ui/tabs/tiles/tab_tiles_unified.cpp']) {
  assert.ok(!read(file).includes('tile_renderer_invalidate_weather_payload'), file);
}

// The night check is the icon's own rule (weather_icons.cpp).
const icons = read('src/types/weather/weather_icons.cpp');
assert.ok(fn(icons, 'for_now').includes('return night_now(sun) ? at_night(icon_name) : icon_name;'));

// Each turn from the cache logs where its time goes, in English.
const reload = fn(tiles, 'tiles_process_reload_requests');
for (const step of ['snapshot', 'restored', 'icons', 'states', 'sensor', 'switch', 'other', 'weather', 'media',
  'show', 'bar', 'schedule', 'finish']) {
  assert.ok(reload.includes(`switch_step("${step}");`), step);
}
assert.ok(tiles.includes('"[Tiles] switch steps folder=%u us: %s\\n"'));
assert.ok(fn(tiles, 'tiles_switch_to_folder').includes('g_switch_requested_us = micros();'));
console.log('Weather: a cached page turn keeps its weather unless payload, date or day/night changed');
