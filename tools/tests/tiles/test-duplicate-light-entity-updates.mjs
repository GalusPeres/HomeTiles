import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = (relativePath) =>
  fs.readFileSync(path.join(repoRoot, relativePath), 'utf8');

const renderer = read('src/tiles/runtime/tile_renderer.cpp');
const popup = read('src/ui/popups/light/light_popup.cpp');
const mqtt = read('src/network/mqtt/mqtt_handlers.cpp');
const tiles = read('src/ui/tabs/tiles/tab_tiles_unified.cpp');

const applyStart = renderer.indexOf('static void apply_switch_tile_state(');
const updateStart = renderer.indexOf('void update_switch_tile_state(', applyStart);
assert.ok(applyStart >= 0 && updateStart > applyStart,
          'Parsed switch-state apply function boundaries were not found');
const updateBody = renderer.slice(applyStart, updateStart);

const visualCheck = updateBody.indexOf('switch_tile_visual_state_equal(prev, state)');
const cacheWrite = updateBody.indexOf('state_target[grid_index] = state;');
const popupUpdate = updateBody.indexOf('update_light_popup(init);');
const visualReturn = updateBody.indexOf('if (tile_visual_unchanged) return;');
// The Switch renderer draws the state (switch_tile_show_state).
const firstStyleWrite = updateBody.indexOf('switch_tile_show_state(');
assert.ok(visualCheck >= 0 && cacheWrite > visualCheck &&
          popupUpdate > cacheWrite && visualReturn > popupUpdate &&
          firstStyleWrite > visualReturn,
          'State cache and bound popup must update before duplicate tile redraw is skipped');

const enqueueStart = renderer.indexOf('static void enqueue_switch_update(');
const batchStart = renderer.indexOf('void queue_switch_tile_updates(', enqueueStart);
const singleStart = renderer.indexOf('void queue_switch_tile_update(', batchStart);
const processStart = renderer.indexOf('void process_switch_update_queue(', batchStart);
assert.ok(enqueueStart >= 0 && batchStart > enqueueStart &&
          singleStart > batchStart && processStart > singleStart,
          'Batched switch queue functions were not found');
const enqueueBody = renderer.slice(enqueueStart, batchStart);
const batchBody = renderer.slice(batchStart, processStart);
const singleBody = renderer.slice(singleStart, processStart);
const processBody = renderer.slice(processStart);
const parsePosition = processBody.indexOf(
  'upd.parsed_state = parse_switch_payload(upd.payload.c_str())');
const fanoutPosition = processBody.indexOf('for (uint8_t grid_index');
const applyPosition = processBody.indexOf('apply_switch_tile_state(', fanoutPosition);
assert.ok(parsePosition >= 0 && fanoutPosition > parsePosition &&
          applyPosition > fanoutPosition,
          'A duplicate entity payload must be parsed once before slot fan-out');
assert.match(enqueueBody,
             /pending\.entity_id\.equalsIgnoreCase\(entity_id\)[\s\S]*pending\.grid_indices = grid_indices;[\s\S]*pending\.layout_generation = layout_generation;[\s\S]*pending\.parsed = false;/,
             'A newer entity payload must replace all targets and force one reparse');
assert.match(batchBody,
             /capture_switch_update_targets[\s\S]*enqueue_switch_update\(grid_type, grid_indices, entity_id, true, payload\);/,
             'A batched live update must capture and bind one entity');
assert.match(singleBody,
             /enqueue_switch_update\(grid_type, uint64_t\{1\} << grid_index, String\(\), false,[\s\S]*payload\);/,
             'A hidden-cache single update must not resolve through the active folder config');
assert.match(processBody,
             /upd\.layout_generation != switch_layout_generation\(upd\.grid_type\)[\s\S]*upd\.grid_indices = 0;[\s\S]*upd\.grid_indices != 0 && !upd\.parsed/,
             'A replaced layout must discard queued slots before payload parsing');
assert.match(processBody,
             /upd\.grid_indices &= ~bit;[\s\S]*if \(upd\.require_entity_match\)[\s\S]*tile->type != TILE_SWITCH[\s\S]*tile->sensor_entity\.equalsIgnoreCase\(upd\.entity_id\)/,
             'Queued slots must be consumed only after their entity binding is revalidated');
assert.match(processBody,
             /\+\+processed;[\s\S]*processed >= max_updates[\s\S]*return;/,
             'The processing budget must count applied slots and preserve a partial batch');

for (const functionName of [
  'static void clear_switch_widgets(',
  'void tile_renderer_restore_tab0(',
]) {
  const start = renderer.indexOf(functionName);
  const nextFunction = renderer.indexOf('\n}', start) + 2;
  assert.ok(start >= 0 && nextFunction > start,
            `Switch layout lifecycle function not found: ${functionName}`);
  assert.match(renderer.slice(start, nextFunction),
               /advance_switch_layout_generation\(/,
               `Queued switch updates must be invalidated by ${functionName}`);
}

const slotResetStart = renderer.indexOf('void reset_switch_widget(');
const allResetStart = renderer.indexOf('void reset_switch_widgets(', slotResetStart);
assert.ok(slotResetStart >= 0 && allResetStart > slotResetStart,
          'Single switch-slot reset function was not found');
const slotReset = renderer.slice(slotResetStart, allResetStart);
assert.match(slotReset, /invalidate_queued_switch_slot\(grid_type, grid_index\);/,
             'A single rebuilt slot must scrub only its own queued work');
assert.doesNotMatch(slotReset, /advance_switch_layout_generation\(/,
                    'A single rebuilt slot must not invalidate unrelated lights');

const slotInvalidationStart = renderer.indexOf(
  'static void invalidate_queued_switch_slot(',
  renderer.indexOf('static SwitchUpdate g_switch_queue'));
const clampRgbStart = renderer.indexOf('static uint32_t clamp_rgb(',
                                       slotInvalidationStart);
assert.ok(slotInvalidationStart >= 0 && clampRgbStart > slotInvalidationStart,
          'Queued switch-slot invalidation helper was not found');
assert.match(
  renderer.slice(slotInvalidationStart, clampRgbStart),
  /pending\.grid_type == grid_type[\s\S]*pending\.grid_indices &= ~bit;/,
  'Slot invalidation must preserve every other bit in the queued batch');

const sensorUpdateStart = tiles.indexOf('void tiles_update_sensor_by_entity(');
const weatherUpdateStart = tiles.indexOf(
  'void tiles_update_weather_by_entity(', sensorUpdateStart);
assert.ok(sensorUpdateStart >= 0 && weatherUpdateStart > sensorUpdateStart,
          'Unified entity update function boundaries were not found');
const sensorUpdateBody = tiles.slice(sensorUpdateStart, weatherUpdateStart);
assert.match(sensorUpdateBody, /uint64_t switch_indices = 0;/,
             'Duplicate switch slots must be collected in one mask');
assert.match(sensorUpdateBody,
             /queue_switch_tile_updates\(grid_type, switch_indices, value\);/,
             'Duplicate switch slots must produce one queued payload');

const reloadStart = tiles.indexOf('void tiles_reload_layout(');
const reloadEnd = tiles.indexOf('void tiles_request_reload(', reloadStart);
const folderStart = tiles.indexOf('void tiles_switch_to_folder(');
const folderEnd = tiles.indexOf('void tiles_cancel_folder_switch(', folderStart);
assert.match(tiles.slice(reloadStart, reloadEnd), /hide_light_popup\(\);/,
             'A layout replacement must close a slot-bound light popup');
assert.match(tiles.slice(folderStart, folderEnd), /viewNavigationClosePopups\(\);/,
             'A folder switch must close a slot-bound light popup');
assert.match(read('src/ui/navigation/view_navigation.cpp'), /hide_light_popup\(\);/,
             'Shared popup teardown must include Light');

const cacheBuildStart = tiles.indexOf('static void build_folder_cache_entry(');
assert.ok(cacheBuildStart >= 0,
          'Folder cache build transaction was not found');
const cacheBuildEnd = tiles.indexOf('\n}', cacheBuildStart) + 2;
const cacheBuild = tiles.slice(cacheBuildStart, cacheBuildEnd);
const preSwapDrain = cacheBuild.indexOf('process_switch_update_queue();');
const activeSnapshot = cacheBuild.indexOf(
  'tile_renderer_snapshot_tab0(g_cache_build_saved_widgets);');
assert.ok(preSwapDrain >= 0 && activeSnapshot > preSwapDrain,
          'Active light updates must drain before a hidden folder pointer swap');

const popupStart = popup.indexOf('void update_light_popup(');
const preloadStart = popup.indexOf('void preload_light_popup()', popupStart);
assert.ok(popupStart >= 0 && preloadStart > popupStart,
          'Light popup update function boundaries were not found');
const popupBody = popup.slice(popupStart, preloadStart);
assert.match(popupBody,
             /has_tile_ref[\s\S]*tile_grid != init\.tile_grid[\s\S]*tile_index != init\.tile_index/,
             'Duplicate entities must not rebind an open popup to another tile');

const routeStart = mqtt.indexOf('static void rebuildDynamicRoutes(');
const weatherStart = mqtt.indexOf('static void rebuildDynamicWeatherRoutes(', routeStart);
assert.ok(routeStart >= 0 && weatherStart > routeStart,
          'Dynamic route function boundaries were not found');
const routeBody = mqtt.slice(routeStart, weatherStart);
assert.match(routeBody, /find_if[\s\S]*r\.topic == topic/,
             'Duplicate entities must retain one MQTT topic route');

const visibleState = {
  available: true,
  hasState: true,
  isOn: true,
  hasColor: false,
  color: 0,
  hasColorTemp: false,
  colorTemp: 4000,
  supportsColor: false,
  supportsBrightness: true,
  supportsTemperature: false,
  modesKnown: true,
  onOffOnly: false,
};
const visualSignature = (state) => JSON.stringify(visibleStateKeys(state));
const visibleStateKeys = (state) => ({
  available: state.available,
  hasState: state.hasState,
  isOn: state.isOn,
  hasColor: state.hasColor,
  color: state.hasColor ? state.color : null,
  hasColorTemp: state.hasColorTemp,
  colorTemp: state.hasColorTemp ? state.colorTemp : null,
  supportsColor: state.supportsColor,
  supportsBrightness: state.supportsBrightness,
  supportsTemperature: state.supportsTemperature,
  modesKnown: state.modesKnown,
  onOffOnly: state.onOffOnly,
});

const brightnessEcho = {...visibleState, brightness: 18};
const nextBrightnessEcho = {...visibleState, brightness: 77};
assert.equal(visualSignature(brightnessEcho), visualSignature(nextBrightnessEcho),
             'Brightness-only feedback must not redraw duplicate tiles');
assert.notEqual(visualSignature(visibleState),
                visualSignature({...visibleState, isOn: false}),
                'On/off feedback must still redraw every duplicate tile');
assert.notEqual(visualSignature(visibleState),
                visualSignature({...visibleState, hasColor: true, color: 0x336699}),
                'Color feedback must still redraw every duplicate tile');

// Behavioral model for the queue contract. This supplements the source checks
// with the regression cases that originally caused the S3 drag backlog.
const slots = new Map();
const pending = [];
const layoutGenerations = new Map([['tab0', 1]]);
let parseCount = 0;
const enqueue = (grid, entity, indices, payload, requireEntityMatch = true) => {
  const existing = pending.find(
    (item) => item.grid === grid &&
      item.requireEntityMatch === requireEntityMatch &&
      (requireEntityMatch
        ? item.entity.toLowerCase() === entity.toLowerCase()
        : item.indices.size === 1 && indices.length === 1 &&
          item.indices.has(indices[0])));
  if (existing) {
    existing.indices = new Set(indices);
    existing.payload = payload;
    existing.generation = layoutGenerations.get(grid);
    existing.parsed = false;
    return;
  }
  pending.push({
    grid,
    entity,
    indices: new Set(indices),
    payload,
    generation: layoutGenerations.get(grid),
    requireEntityMatch,
    parsed: false,
  });
};
const process = (maxUpdates) => {
  let applied = 0;
  while (pending.length && (!maxUpdates || applied < maxUpdates)) {
    const item = pending[0];
    if (item.generation !== layoutGenerations.get(item.grid)) {
      pending.shift();
      continue;
    }
    if (!item.parsed) {
      item.state = JSON.parse(item.payload);
      item.parsed = true;
      ++parseCount;
    }
    for (const index of [...item.indices].sort((a, b) => a - b)) {
      item.indices.delete(index);
      const slot = slots.get(`${item.grid}:${index}`);
      if (item.requireEntityMatch &&
          (!slot || slot.toLowerCase() !== item.entity.toLowerCase())) continue;
      slots.set(`${item.grid}:${index}:state`, item.state.value);
      ++applied;
      if (maxUpdates && applied >= maxUpdates) break;
    }
    if (item.indices.size) break;
    pending.shift();
  }
  return applied;
};
const invalidateSlot = (grid, index) => {
  for (const item of pending) {
    if (item.grid === grid) item.indices.delete(index);
  }
};

slots.set('tab0:0', 'light.desk');
slots.set('tab0:1', 'light.desk');
slots.set('tab0:2', 'light.desk');
enqueue('tab0', 'light.desk', [0, 1, 2], '{"value":10}');
assert.equal(process(1), 1, 'One call must not exceed its slot budget');
assert.equal(parseCount, 1, 'A partial batch must parse its payload once');
assert.equal(pending[0].indices.size, 2, 'Unapplied duplicate slots must remain queued');
assert.equal(process(1), 1, 'The next call must resume one remaining slot');
assert.equal(parseCount, 1, 'Resuming a partial batch must not reparse');

enqueue('tab0', 'light.desk', [0, 1, 2], '{"value":20}');
assert.equal(process(2), 2, 'A newer payload must still obey the slot budget');
assert.equal(parseCount, 2, 'A newer payload must be parsed exactly once');
assert.equal(process(2), 1, 'The newest payload must reach the final duplicate');
for (const index of [0, 1, 2]) {
  assert.equal(slots.get(`tab0:${index}:state`), 20,
               'Every duplicate must end on the newest state');
}

slots.set('tab0:1', 'light.reassigned');
enqueue('tab0', 'light.desk', [0, 1], '{"value":30}');
assert.equal(process(0), 1, 'A reassigned slot must be discarded, not applied');
assert.equal(slots.get('tab0:1:state'), 20,
             'A stale entity update must not touch a reassigned slot');

enqueue('tab0', 'light.desk', [0, 2], '{"value":35}');
invalidateSlot('tab0', 0);
assert.equal(process(1), 1,
             'Rebuilding one slot must preserve another slot queued with it');
assert.equal(slots.get('tab0:0:state'), 30,
             'Old work for the rebuilt slot must be discarded');
assert.equal(slots.get('tab0:2:state'), 35,
             'An unrelated duplicate must still receive the queued update');

const parsedBeforeStaleLayout = parseCount;
enqueue('tab0', '', [0], '{"value":40}', false);
layoutGenerations.set('tab0', 2);
assert.equal(process(0), 0,
             'A single-slot update must be dropped after its layout is replaced');
assert.equal(parseCount, parsedBeforeStaleLayout,
             'A stale-layout payload must be discarded before parsing');
assert.equal(slots.get('tab0:0:state'), 30,
             'A stale single-slot update must not touch replacement widgets');

enqueue('tab0', '', [2], '{"value":50}', false);
assert.equal(process(1), 1,
             'A same-generation hidden-cache update must retain legacy delivery');
assert.equal(slots.get('tab0:2:state'), 50,
             'A hidden-cache update must not depend on the active folder entity');

console.log('Duplicate light entity update contract: PASS');
