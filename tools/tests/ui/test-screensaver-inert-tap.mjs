// A tap on a screensaver tile that carries no action of its own was swallowed:
// the screensaver disables popups and controls in this mode (sensor, cover,
// weather graph renderers), the card removes LV_OBJ_FLAG_EVENT_BUBBLE, and the
// wallpaper's click handler never saw the touch. Such a tap now leaves the
// screensaver for the panel, while the tiles that do work keep their action.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = (name) => fs.readFileSync(path.join(root, name), 'utf8').replace(/\r\n?/g, '\n');

const policy = read('src/types/tile_type_policy.h');
const inert = /static constexpr bool tileTypeScreensaverTapIsInert\(int type\) \{([\s\S]*?)\n\}/.exec(policy);
assert.ok(inert, 'tileTypeScreensaverTapIsInert exists');
const body = inert[1];

// The inert types: no popup and no control reaches them on the screensaver.
for (const type of ['TILE_SENSOR', 'TILE_ENERGY', 'TILE_BINARY_SENSOR',
                    'TILE_COVER', 'TILE_WEATHER_GRAPH', 'tileTypeIsEditableValue(type)']) {
  assert.ok(body.includes(type), `inert on the screensaver: ${type}`);
}
// The types that still act on a tap must not exit the screensaver instead.
for (const type of ['TILE_SWITCH', 'TILE_SCENE', 'TILE_MEDIA', 'TILE_EMPTY']) {
  assert.ok(!body.includes(type), `an active tile must keep its action: ${type}`);
}

const saver = read('src/ui/screensaver/image_screensaver.cpp');
// The callback itself and its wiring on the built card.
assert.match(saver, /static void on_inert_slot_tile_clicked\(lv_event_t\* e\) \{\s*\(void\)e;\s*hide_image_screensaver\(\);/);
assert.match(saver,
  /lv_obj_remove_flag\(tile_obj, LV_OBJ_FLAG_EVENT_BUBBLE\);\s*\/\/[^\n]*\n(?:\s*\/\/[^\n]*\n)*\s*if \(tileTypeScreensaverTapIsInert\(tile\.type\)\) \{\s*lv_obj_add_event_cb\(tile_obj, on_inert_slot_tile_clicked, LV_EVENT_CLICKED, nullptr\);/,
  'build_slot_tile wires the exit tap for inert tiles');
// The policy header the screensaver reads must be included explicitly.
assert.match(saver, /#include "src\/types\/tile_type_policy\.h"/);
// A tap on the wallpaper still leaves the screensaver the same way.
assert.match(saver, /if \(g_state && lv_event_get_target\(e\) == g_state->overlay\) \{\s*hide_image_screensaver\(\);/);

console.log('Screensaver: a tap on an inert tile exits to the panel');
