import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = relative => fs.readFileSync(path.join(repoRoot, relative), 'utf8')
  .replace(/\r\n?/g, '\n');
const header = read('src/tiles/config/tile_config.h');
const config = read('src/tiles/config/tile_config.cpp');
const screensaver = read('src/ui/screensaver/screensaver_config.cpp');

const persistedIds = [
  'EMPTY', 'SENSOR', 'SCENE', 'KEY', 'FOLDER', 'SWITCH', 'IMAGE', 'SETTINGS',
  'BACK', 'CLOCK', 'TEXT', 'COUNTER', 'WEATHER', 'RADAR', 'ENERGY', 'MEDIA',
  'PIXELANIM', 'CLIMATE', 'CAMERA', 'COVER', 'BINARY_SENSOR', 'NUMBER', 'SELECT', 'DATETIME',
];
// These expected sets characterize the pre-refactor consumers independently.
const policies = [
  ['isRetiredTileType', [3, 6, 11, 13]],
  ['entityTileStoresSensorEntity', [1, 5, 12, 14, 15, 17, 18, 19, 20, 21, 22, 23]],
  ['tileTypeUsesCachedEntityState', [1, 5, 12, 14, 15, 17, 19, 20, 21, 22, 23]],
  ['tileTypeStoresPopupMode', [1, 5, 12, 14, 17, 19, 20, 21, 22, 23]],
  ['tileTypeStoresPopupModeDirectly', [1, 12, 14, 17, 19, 20, 21, 22, 23]],
  ['tileTypeHasDynamicMqttRoute', [1, 5, 12, 15, 17, 19, 20, 21, 22, 23]],
  ['tileTypeSubscribesDynamicState', [1, 5, 14, 15, 17, 19, 20, 21, 22, 23]],
  ['tileTypeSubscribesScreensaverState', [1, 5, 14, 15, 19, 20, 21, 22, 23]],
  ['tileTypeAllowedInScreensaver', [0, 1, 2, 5, 14, 15, 19, 20, 21, 22, 23]],
  ['tileTypeRefreshesEntityIcon', [1, 2, 5, 14, 15, 17, 19, 20, 21, 22, 23]],
];

function requiredMatch(source, pattern, label) {
  const match = source.match(pattern);
  assert.ok(match, `${label} was not found`);
  return match[0];
}

const popupEnums = [
  requiredMatch(header, /enum TilePopupOpenMode[^}]*};/, 'Popup mode enum'),
  requiredMatch(header, /enum SwitchPopupOpenModeStorage[^}]*};/, 'Switch popup storage enum'),
].join('\n');
const popupStart = header.indexOf('static inline uint8_t getTilePopupOpenMode(');
const popupEnd = header.indexOf('\nstruct TileGridConfig', popupStart);
assert.ok(popupStart >= 0 && popupEnd > popupStart);
const popupFunctions = header.slice(popupStart, popupEnd);
const normalizeScreensaver = requiredMatch(screensaver,
  /TileType normalize_screensaver_type\(int raw\) \{[\s\S]*?\n\}/,
  'Screensaver normalization');
const packedStruct = requiredMatch(config,
  /struct PackedTileV7 \{[\s\S]*?\n\};/, 'PackedTileV7');
const packedStringSizes = ['TITLE', 'ICON', 'ENTITY', 'UNIT', 'SCENE', 'MACRO']
  .map(name => requiredMatch(config,
    new RegExp(`static constexpr size_t ${name}_MAX\\s*=\\s*\\d+;`),
    `${name}_MAX`)).join('\n');
const packPopup = requiredMatch(config,
  /out\.popup_open_mode = \(tileTypeStoresPopupMode\(in\.type\)[\s\S]*?;/,
  'Packed popup assignment');

for (const [file, markers] of [
  ['src/tiles/config/tile_config.cpp', ['entityTileStoresSensorEntity(tile.type)',
    'tileTypeStoresPopupMode(in.type)', 'tileTypeStoresPopupMode(out.type)']],
  ['src/ui/tabs/tiles/tab_tiles_unified.cpp', ['tileTypeUsesCachedEntityState(tile.type)',
    'tileTypeUsesCachedEntityState(slot.type)', 'tileTypeRefreshesEntityIcon(tile.type)']],
  ['src/network/mqtt/mqtt_handlers.cpp', ['tileTypeSubscribesDynamicState(slot.type)',
    'tileTypeSubscribesScreensaverState(tile.type)']],
  ['src/web/server/handlers/web_admin_tiles.cpp', ['tileTypeHasDynamicMqttRoute(tile.type)',
    'tileTypeAllowedInScreensaver(type)']],
]) {
  const source = read(file);
  for (const marker of markers) assert.ok(source.includes(marker), `${file}: ${marker}`);
}

const source = `
#include "src/types/tile_type_policy.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
${persistedIds.map((name, value) =>
  `static_assert(TILE_${name} == ${value}, "Persisted ${name} ID changed");`).join('\n')}
static_assert(sizeof(TileType) == 1, "Persisted TileType width changed");
${packedStringSizes}
${packedStruct}
static_assert(sizeof(PackedTileV7) == 240, "PackedTileV7 size changed");
static_assert(offsetof(PackedTileV7, sensor_entity) == 76, "Entity offset changed");
static_assert(offsetof(PackedTileV7, popup_open_mode) == 236, "Popup offset changed");
${popupEnums}
struct Tile {
  TileType type;
  uint8_t popup_open_mode;
  uint8_t key_code;
  uint8_t key_modifier;
};
${popupFunctions}
${normalizeScreensaver}
uint8_t packedPopup(const Tile& in) {
  PackedTileV7 out{};
  ${packPopup}
  return out.popup_open_mode;
}
${policies.map(([name, ids], index) =>
  `constexpr uint32_t expected${index} = ${ids.reduce((mask, id) => mask + 2 ** id, 0)}u;
static_assert(${name}(static_cast<TileType>(${ids[0]})), "Policy must remain constexpr");`
).join('\n')}
int main() {
  for (unsigned raw = 0; raw < 256; ++raw) {
    const auto type = static_cast<TileType>(raw);
    ${policies.map(([name], index) =>
      `assert(${name}(type) == (raw < 32 && (expected${index} & (uint32_t{1} << raw))));`
    ).join('\n')}
    const bool direct = raw < 32 && (expected4 & (uint32_t{1} << raw));
    const bool stored = raw < 32 && (expected3 & (uint32_t{1} << raw));
    for (uint8_t mode : {uint8_t{0}, uint8_t{1}, uint8_t{2}, uint8_t{255}}) {
      for (uint8_t key : {uint8_t{0}, uint8_t{1}, uint8_t{2}, uint8_t{3}, uint8_t{255}}) {
        Tile tile{type, mode, key, 99};
        const uint8_t expected = raw == 5 ? (key == 2 ? 0 : 1) : (direct && mode == 1 ? 1 : 0);
        assert(getTilePopupOpenMode(tile) == expected);
        assert(packedPopup(tile) == (stored ? expected : 0));
        setTilePopupOpenMode(tile, mode);
        assert(tile.popup_open_mode == ((direct || raw == 5) ? (mode == 1 ? 1 : 0) : mode));
        assert(tile.key_code == (raw == 5 ? (mode == 1 ? 1 : 2) : key));
        assert(tile.key_modifier == (raw == 5 ? 0 : 99));
      }
    }
  }
  for (int raw = -512; raw <= 512; ++raw) {
    const bool allowed = raw >= 0 && raw < 32 && (expected8 & (uint32_t{1} << raw));
    assert(tileTypeAllowedInScreensaver(raw) == allowed);
    const unsigned converted = static_cast<uint8_t>(raw);
    const bool stored = converted < 32 && (expected8 & (uint32_t{1} << converted));
    assert(normalize_screensaver_type(raw) == (stored ? converted : 0));
  }
}
`.replace('#include <assert.h>', '#include <assert.h>\n#include <initializer_list>');

const compiler = [process.env.CXX, 'clang++', 'g++', 'c++'].filter(Boolean)
  .find(candidate => spawnSync(candidate, ['--version'], {encoding: 'utf8'}).status === 0);
if (!compiler) {
  console.log('SKIP: Tile type policy execution requires a C++17 host compiler');
  process.exit(0);
}
const buildDir = path.join(repoRoot, 'build', 'tests', 'tile-type-policy');
fs.mkdirSync(buildDir, {recursive: true});
const sourcePath = path.join(buildDir, 'policy-test.cpp');
const outputPath = path.join(buildDir, process.platform === 'win32' ? 'policy-test.exe' : 'policy-test');
fs.writeFileSync(sourcePath, source);
const compiled = spawnSync(compiler,
  ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', repoRoot, sourcePath, '-o', outputPath],
  {encoding: 'utf8'});
assert.equal(compiled.status, 0, `${compiled.stdout}${compiled.stderr}`);
const run = spawnSync(outputPath, [], {encoding: 'utf8'});
assert.equal(run.status, 0, `${run.stdout}${run.stderr}`);
console.log('Tile type policies, popup semantics, and persistence layout: PASS');
