// Every entity picker searches through the Bridge (user 2026-10-15: the
// Scene tile had been left out and listed only the Bridge's releases). The
// test reads every picker field the tile types render and every tile type
// that stores an entity, so a forgotten type fails here instead of on a
// panel. The Scene tile ("Button" since then) stores the scene, script or
// button id it runs; a tile from before keeps its Bridge alias.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';

import {readRepoFile} from '../../lib/admin-source.mjs';

const root = path.resolve(new URL('../../..', import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, '$1'));

// Picker lists the tile types render (appendEntityPickerField's last literal).
const typeDirs = fs.readdirSync(path.join(root, 'src/types'), {withFileTypes: true})
  .filter((entry) => entry.isDirectory()).map((entry) => entry.name);
const rendered = new Map();
for (const dir of typeDirs) {
  for (const file of fs.readdirSync(path.join(root, 'src/types', dir))) {
    if (!/\.cpp$/.test(file)) continue;
    const text = fs.readFileSync(path.join(root, 'src/types', dir, file), 'utf8');
    for (const call of text.matchAll(/appendEntityPickerField\(([\s\S]*?)\);/g)) {
      // The list is the last string literal (the field name comes first).
      const literals = [...call[1].matchAll(/"([a-z_]+)"/g)].map((m) => m[1]);
      const list = literals[literals.length - 1];
      if (list) rendered.set(list, `${dir}/${file}`);
    }
  }
}
// Lock, Alarm panel and Fan pass their list as a variable (types/device/web.cpp).
for (const list of ['locks', 'alarm_panels', 'fans']) rendered.set(list, 'device/web.cpp');
assert.ok(rendered.has('scenes'), 'the Scene tile renders its picker');

// The panel: every list is one it asks the Bridge for.
const search = readRepoFile('src/network/bridge/entity_search.cpp');
const table = search.slice(search.indexOf('const ListTile kListTiles[] = {'), search.indexOf('};', search.indexOf('const ListTile kListTiles[] = {')));
const tileLists = new Map([...table.matchAll(/\{"([a-z_]+)", (TILE_[A-Z_]+)\}/g)].map((m) => [m[1], m[2]]));
const special = {energy: /if \(list == "energy"\) return haBridgeConfig\.supportsEnergySearch\(\);/,
                 images: /return list == "images";/};
for (const [list, where] of rendered) {
  if (special[list]) {
    assert.match(search, special[list], `${list} (${where}) is a known list`);
  } else {
    assert.ok(tileLists.has(list), `${list} (${where}) is in kListTiles`);
  }
}

// Every tile type that stores an entity has its list (Camera: cameras,
// Energy: the energy list of the Bridge).
const policy = readRepoFile('src/types/tile_type_policy.h');
const stores = policy.slice(policy.indexOf('entityTileStoresSensorEntity'), policy.indexOf('}', policy.indexOf('entityTileStoresSensorEntity')));
const storing = new Set([...stores.matchAll(/TILE_[A-Z_]+/g)].map((m) => m[0]));
if (/tileTypeIsEditableValue\(type\)/.test(stores)) ['TILE_NUMBER', 'TILE_SELECT', 'TILE_DATETIME'].forEach((t) => storing.add(t));
if (/tileTypeIsDeviceControl\(type\)/.test(stores)) ['TILE_LOCK', 'TILE_ALARM', 'TILE_FAN'].forEach((t) => storing.add(t));
const listed = new Set(tileLists.values());
for (const type of storing) {
  if (type === 'TILE_ENERGY') continue;
  assert.ok(listed.has(type), `${type} stores an entity and has a picker list`);
}
assert.ok(storing.has('TILE_SCENE'), 'the Scene tile keeps its entity id like every entity tile');

// The browser asks the Bridge for every list as well.
const picker = readRepoFile('src/web/admin/tiles/entity-picker.js');
const remote = new Set([...picker.slice(picker.indexOf('const ENTITY_REMOTE_LISTS'), picker.indexOf(']);', picker.indexOf('const ENTITY_REMOTE_LISTS'))).matchAll(/'([a-z_]+)'/g)].map((m) => m[1]));
for (const [list, where] of rendered) assert.ok(remote.has(list), `${list} (${where}) is searched in the browser`);

// The Scene tile: its target, stored and read through one rule.
const config = readRepoFile('src/tiles/config/tile_config.h');
assert.match(config, /static inline const String& sceneTarget\(const Tile& tile\) \{\s*return sceneTargetIsEntityId\(tile\.sensor_entity\) \? tile\.sensor_entity : tile\.scene_alias;/);
assert.match(config, /static inline void setSceneTarget\(Tile& tile, const String& value\)/);
const targetRule = readRepoFile('src/types/scene/scene_target.h');
for (const domain of ['scene', 'script', 'button', 'input_button']) {
  assert.match(targetRule, new RegExp(`value\\.startsWith\\("${domain}\\."\\)`), `${domain} ids are entity ids`);
}
assert.match(readRepoFile('src/types/scene/web_handler.cpp'), /setSceneTarget\(tile, scene_entity_for\(server\.hasArg\("scene_alias"\)/);
const renderer = readRepoFile('src/types/scene/renderer.cpp');
assert.match(renderer, /new SceneEventData\{sceneTarget\(tile\), scene_cb\}/, 'a tap runs the target');
assert.match(renderer, /haBridgeConfig\.findSceneEntity\(sceneTarget\(tile\)\)/, 'the icon comes from the target');
assert.doesNotMatch(renderer, /tile\.scene_alias/, 'nothing reads the alias alone');
assert.match(readRepoFile('src/ui/tabs/tiles/tab_tiles_unified.cpp'), /haBridgeConfig\.findSceneEntity\(sceneTarget\(tile\)\)/);
assert.match(readRepoFile('src/web/server/render/web_admin_html.cpp'), /haBridgeConfig\.findSceneEntity\(sceneTarget\(tile\)\)/);
assert.match(readRepoFile('src/web/server/handlers/web_admin_tiles.cpp'),
  /appendJsonEscaped\(out, tile\.type == TILE_SCENE \? scene_picker_value\(tile\) : tile\.scene_alias\);/, 'the editor gets the target');
assert.match(readRepoFile('src/network/bridge/ha_bridge_config.cpp'), /if \(sceneTargetIsEntityId\(alias\)\) return alias;/);
assert.match(search, /if \(list == "scenes"\) return haBridgeConfig\.supportsSceneSearch\(\);/, 'only a Bridge on level 4 is asked');
assert.match(search, /if \(type == TILE_SCENE && !\(entity && sceneTargetIsEntityId\(String\(entity\)\)\)\) return nullptr;/,
  'only entity ids are declared, an alias is a release already');
assert.match(readRepoFile('src/network/bridge/ha_bridge_config.h'), /bool supportsSceneSearch\(\) const \{ return entity_search_ >= 4; \}/);

// The browser's rule matches the panel's.
const state = readRepoFile('src/web/admin/tiles/state.js');
const body = state.slice(state.indexOf('function sceneEntityOf('), state.indexOf('}', state.indexOf('function sceneEntityOf(')) + 1);
const sceneEntityOf = new Function('sensorMetaCache', `${body}; return sceneEntityOf;`)({sceneEntities: {tv: 'scene.tv'}});
assert.equal(sceneEntityOf('button.wolf_fhs280_uhrzeit_synchronisieren'), 'button.wolf_fhs280_uhrzeit_synchronisieren');
assert.equal(sceneEntityOf('tv'), 'scene.tv', 'an alias maps to its entity');
assert.equal(sceneEntityOf('scenes_x'), '', 'no dot, no entity id');
assert.equal(sceneEntityOf(''), '');
for (const file of ['grid-preview.js', 'live-preview.js']) {
  assert.match(readRepoFile(`src/web/admin/tiles/${file}`), /sceneEntityOf\(/, `${file} finds the icon of a stored id`);
}

console.log(`Every picker searches through the Bridge: ${rendered.size} lists, ${storing.size} entity tile types`);
