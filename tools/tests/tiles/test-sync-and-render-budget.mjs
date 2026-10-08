// Render and sync budget (analysis 2026-10-07): popups and folders draw from
// caches, repeated Bridge states cost no redraw or log line, energy and the
// periodic refresh only ask when there is something to get, and a lost
// message is repaired without the minute poll.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = file => fs.readFileSync(path.join(root, file), 'utf8').replace(/\r\n?/g, '\n');
const fn = (source, name) => {
  const found = cppFunctionDefinitions(source).find(f => f.name === name);
  assert.ok(found, `${name} is missing`);
  return found.source;
};

// LVGL draw caches: the popup card shadow corner (shadow width + card radius)
// fits the shadow cache on every layout; many radii fit the circle cache.
const conf = read('lv_conf.h');
assert.match(conf, /#if defined\(DEVICE_ESP32_S3_RGB_480\)\n#define LV_DRAW_SW_SHADOW_CACHE_SIZE 48\n#else\n#define LV_DRAW_SW_SHADOW_CACHE_SIZE 72\n#endif/);
assert.match(conf, /#define LV_DRAW_SW_CIRCLE_CACHE_SIZE 32\n/);
// lv_draw_sw_box_shadow.c caches only when (blur + radius)^2 < size^2. The
// popup card's radius is the tile radius plus a grid gap (b284), so the blur
// follows the object's radius: as wide as still fits, at most 28 px
// (popup_layout.h shadow_width_for(), 2026-10-08; with 28 px the V2 missed
// the cache and recomputed the corner for every refresh band).
const popupLayout = read('src/ui/popups/popup_layout.h');
assert.match(popupLayout, /inline int shadow_width_for\(int radius\) \{\s*const int room = kShadowCacheSize - 1 - radius;\s*return room <= 0 \? 0 : room < kCardShadowMax \? room : kCardShadowMax;/);
assert.match(popupLayout, /lv_obj_set_style_shadow_width\(obj, shadow_width_for\(lv_obj_get_style_radius\(obj, LV_PART_MAIN\)\), 0\);/);
assert.match(popupLayout, /constexpr bool kCardFillsScreen = kCardWidth == SCREEN_WIDTH && kCardHeight == SCREEN_HEIGHT;/);
// Every panel's largest card radius still leaves a visible blur (classic
// grid: one grid gap as the margin, device.h).
for (const [name, h, rows, gap, minimum, cache] of [['V2', 800, 5, 16, 22, 72], ['Tab5', 720, 4, 16, 22, 72],
  ['7B', 600, 4, 16, 22, 72], ['4B', 720, 4, 16, 22, 72], ['S3', 480, 4, 10, 15, 48], ['4.3', 480, 4, 10, 15, 72],
  ['JC4880', 800, 6, 10, 15, 72]]) {
  const cellH = Math.floor((h - (rows + 1) * gap) / rows);
  const tileMax = Math.floor((cellH - gap + 2) / 4);
  const cardRadius = tileMax + gap;  // kCardRadius == tile_radius::kMinimum
  const blur = Math.min(name === 'S3' || name === '4.3' || name === 'JC4880' ? 19 : 28, cache - 1 - cardRadius);
  assert.ok(blur >= 8 && blur + cardRadius < cache && minimum > 0, `${name}: blur ${blur} at radius ${cardRadius}`);
}
// The popup shell's frame takes the blur for its own radius on every open.
assert.match(read('src/ui/popups/popup_shell.cpp'), /popup_layout::shadow_width_for\(lv_obj_get_style_radius\(shell\.frame, LV_PART_MAIN\)\)/);

// Folder caches: both chips warm Back and the visible Folder tiles while idle;
// the S3 waits longer and never builds during a burst of Bridge messages.
const folders = read('src/ui/tabs/tiles/tab_tiles_unified.cpp');
assert.ok(!folders.includes('can_preload_more_folders'), 'the 384 KB boot gate never passed on the S3');
assert.match(folders, /#else\nstatic constexpr size_t kMaxResidentFolderUiCaches = 4;[\s\S]*?kMaxNavigationPreloadTargets = 3;[\s\S]*?kNavigationPreloadIdleMs = 1500;[\s\S]*?kNavigationPreloadQuietMs = 300;/);
const preload = fn(folders, 'process_navigation_preload');
assert.match(preload, /mqttInboundBusy\(kNavigationPreloadQuietMs\)/);
assert.ok(!/#if defined\(CONFIG_IDF_TARGET_ESP32P4\)\nstatic void process_navigation_preload/.test(folders),
          'navigation preload must not be P4-only');
const evict = fn(folders, 'find_folder_cache_eviction_candidate');
assert.match(evict, /if \(!best && !for_preload\) return root_fallback;/, 'a preload never displaces Home on the S3');
assert.match(fn(folders, 'folder_cache_post_build_reserve_ok'), /kFolderCachePreloadMinInternalAfterBuild/);

// Repeated states: only a new payload is logged, and the label keeps its text.
const update = fn(folders, 'tiles_update_sensor_by_entity');
assert.match(update, /const bool changed = cache_entity_payload\(entity_id, value\);/);
assert.equal((update.match(/Serial\.printf/g) || []).length,
             (update.match(/if \(changed && tile_queue_log_due\(grid_type, i\)\) \{\n\s*Serial\.printf/g) || []).length,
             'every queued log line must be gated on a changed payload and the per-tile 30 s limit');
assert.match(fn(folders, 'tile_queue_log_due'), /now - last < 30000UL/);
const renderer = read('src/tiles/runtime/tile_renderer.cpp');
assert.match(fn(renderer, 'update_sensor_tile_value'),
             /if \(!shown \|\| strcmp\(shown, combined\.c_str\(\)\) != 0\) \{\n\s*lv_label_set_text\(value_label, combined\.c_str\(\)\);/);
// A full-grid cache application fits the sensor queue; an overflow repairs
// the visible grid from the entity cache.
assert.match(renderer, /QUEUE_SIZE =\n\s*TILES_PER_GRID \+ 8 > 32/);
assert.match(fn(folders, 'tiles_process_visible_cache_refresh'), /tile_renderer_take_sensor_queue_overflow\(\)/);

// Web Admin values: the index is live, the text blob is built on demand.
const bridge = read('src/network/bridge/ha_bridge_config.cpp');
const updateValue = fn(bridge, 'HaBridgeConfig::updateSensorValue');
assert.ok(!updateValue.includes('.substring('), 'a live value must not rebuild the whole blob');
assert.match(updateValue, /values_blob_dirty_ = true;/);
assert.match(read('src/network/bridge/ha_bridge_config.h'), /if \(values_blob_dirty_\) materializeValuesBlob\(\);\n\s*return data;/);
assert.match(fn(bridge, 'HaBridgeConfig::applyJson'),
             /if \(values_blob_dirty_\) materializeValuesBlob\(\);\n\s*HaBridgeConfigData merged = data;/);
assert.match(fn(bridge, 'HaBridgeConfig::applyJson'), /"\\"push\\""/);

// Energy: only with an energy section, every five minutes, and no endless
// 15-second retry when the Bridge never answers.
const energy = read('src/types/energy/energy_data.cpp');
assert.match(energy, /kEnergyPeriodicMs = 5UL \* 60UL \* 1000UL;/);
assert.match(fn(energy, 'energy_service_periodic'), /if \(!haBridgeConfig\.hasEnergyEntries\(\)\) return;/);
assert.match(fn(energy, 'service_energy_retry'), /state\.retry_requested = state\.timeouts < kEnergyMaxTimeoutsInRow;/);
assert.match(fn(energy, 'queue_energy_response'), /request\.timeouts = 0;/);

// Periodic refresh: a "push" Bridge gets the safety interval; a dropped
// message asks again after 5 s, at most every 30 s.
const sketch = read('HomeTiles.ino');
const refresh = fn(sketch, 'service_background_state_refresh');
assert.match(refresh, /haBridgeConfig\.bridgePushesChanges\(\)\n\s*\? BACKGROUND_STATE_SAFETY_REFRESH_MS\n\s*: BACKGROUND_STATE_REFRESH_MS;/);
assert.match(refresh, /mqttTakeInboundDropped\(\)/);
assert.match(sketch, /BACKGROUND_STATE_SAFETY_REFRESH_MS = 15UL \* 60UL \* 1000UL;/);
assert.match(sketch, /DROP_REPAIR_MIN_GAP_MS = 30000UL;/);
// Drops inside the answer's own burst back off instead of asking every 30 s.
assert.match(refresh, /now - g_last_bridge_state_refresh_ms < DROP_REPAIR_ANSWER_WINDOW_MS[\s\S]*?g_drop_repair_gap_ms \* 2;/);
assert.match(refresh, /since_last >= g_drop_repair_gap_ms/);
const mqtt = read('src/network/mqtt/mqtt_handlers.cpp');
assert.equal((mqtt.match(/g_inbound_dropped = true;/g) || []).length, 2,
             'both drop paths (no memory, full queue) must request a repair');

// perf2: a finger on the panel holds background tile states (max 1.5 s).
const display = read('src/core/display/display_manager.cpp');
assert.match(fn(display, 'DisplayManager::touch_cb'), /g_touch_held = false;[\s\S]*data->state = LV_INDEV_STATE_PRESSED;[\s\S]*g_touch_held = true;/);
assert.match(fn(sketch, 'tile_updates_held_for_touch'), /now - held_since_ms < TILE_HOLD_MAX_MS/);
assert.match(sketch, /TILE_HOLD_MAX_MS = 1500UL;/);
assert.match(sketch, /if \(!hold_tile_updates\) process_tile_update_queues<TileUpdateBudget::Active>\(\);/);

// perf2: the S3 preloads at most three grids, with the growth reserve, and
// gives a hidden grid back under memory pressure.
assert.match(folders, /kFolderCachePreloadMinInternalAfterBuild = 40UL \* 1024UL;/);
assert.match(folders, /kFolderCachePreloadMinLargestInternalAfterBuild = 16UL \* 1024UL;/);
assert.match(folders, /kMaxPreloadResidentGrids = 3;/);
assert.match(preloadAfter(), /resident_folder_cache_count\(\) >= kMaxPreloadResidentGrids/);
assert.match(preloadAfter(), /!folder_cache_post_build_reserve_ok\(folder_cache_memory_snapshot\(\)\)/);
assert.match(fn(folders, 'release_folder_cache_under_pressure'), /kFolderCachePressureInternalBytes[\s\S]*evict_folder_cache_before_build\(kInvalidFolderId, false\)/);
function preloadAfter() { return fn(read('src/ui/tabs/tiles/tab_tiles_unified.cpp'), 'process_navigation_preload'); }

// perf2: popups switching ranges reuse answers younger than a minute.
assert.match(fn(energy, 'energy_request_period'), /force && !request\.retry_requested && request\.last_response_ms != 0 &&\s*\(uint32_t\)\(now - request\.last_response_ms\) < kEnergyFreshMs/);
assert.match(fn(mqtt, 'mqttPublishHistoryRequest'), /replay_history_response\(entity_id, hours, period_minutes, points\)/);
assert.match(fn(mqtt, 'replay_history_response'), /now - entry\.stored_ms >= kHistoryReplayMaxAgeMs/);
assert.match(mqtt, /response_hours == request->hours &&\s*response_period == request->period_minutes/,
             'a replay is stored only for the range that is still pending');
assert.ok(!mqtt.includes('HA living area temperature'), 'the legacy temperature print is gone');

console.log('Sync and render budget: draw caches, S3 idle preload, quiet repeats, lazy Web values, energy and refresh pacing: PASS');
