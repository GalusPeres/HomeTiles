// A page turn re-applies every cached state; Number, Select and Date/Time
// tiles parsed their payload and set label, height and long mode again each
// time (V2 b598 switch steps: page 1 "states" 11 ms with four editable tiles,
// 4-5 ms on pages without). A label that already shows the payload, in the
// panel language and the current editable configuration, keeps it; a newly
// rendered tile and every cleared widget start without a signature.
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

const widgets = read('src/types/sensor/widgets.h');
assert.ok(widgets.includes('uint32_t editable_hash = 0;'), 'the label remembers what it shows');

const control = read('src/types/value/value_control.cpp');
const refresh = fn(control, 'refresh_editable_tile');
const check = refresh.indexOf('if (widgets[index].editable_hash == shown) return;');
assert.ok(check > 0, 'an unchanged label returns early');
assert.ok(refresh.indexOf('const uint32_t shown = shown_signature(payload, configManager.getConfig().language);') < check);
assert.ok(check < refresh.indexOf('parse_editable_value(payload)') && check < refresh.indexOf('lv_label_set_long_mode('),
  'before parsing and before touching the label');
assert.equal((refresh.match(/findEditableValue\(/g) || []).length, 1, 'the payload is read once');

const signature = fn(control, 'shown_signature');
assert.ok(signature.includes('mix(payload.c_str());') && signature.includes('mix(language);') &&
  signature.includes('configuration_generation') && signature.includes('return hash ? hash : 1u;'),
  'payload, language and configuration; never the empty 0');
assert.ok(fn(control, 'editable_configuration_changed').includes('++configuration_generation;'),
  'a configuration change shows every label again');

// Every place that sets up or clears a sensor widget starts it over.
assert.ok(read('src/types/sensor/renderer.cpp').includes('target[index].editable_hash = 0;'), 'a rendered tile');
assert.ok(fn(read('src/tiles/runtime/tile_renderer.cpp'), 'clear_sensor_widgets').includes('target[i].editable_hash = 0;'),
  'a cleared grid');
assert.ok(fn(read('src/tiles/runtime/tile_renderer.cpp'), 'reset_sensor_widget').includes('target[grid_index] = {};'),
  'a reset slot');

// Only refresh_editable_tile writes an editable tile's value label: live
// sensor updates take Sensor and Energy tiles only.
const tiles = read('src/ui/tabs/tiles/tab_tiles_unified.cpp');
assert.ok(fn(tiles, 'enqueue_cached_tile_state').includes(
  'if (tileTypeIsEditableValue(tile.type)) { refresh_editable_tile(grid_type, index); return; }'));
console.log('Editable tiles: a page turn keeps a label that already shows its payload');
