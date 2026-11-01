// Issue #138: a Button tile picked from the Bridge's scene list stored the
// Bridge alias (the entity id without its domain) in scene_alias, whose 32
// bytes cut "roborock_qrevo_maxv_xian_sao_hou_tuo" to 31 characters; the tile
// then found neither its entity in the Web Admin nor its button on the panel.
// The picker list now offers entity ids, the save handler turns a known alias
// into its entity id (sensor_entity, 63 characters plus the entity sidecar),
// and an alias tile from before shows its entity in the editor. Compiles the
// real save handler (types/scene/web_handler.cpp) with the tile helpers.
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';
import {compileAndRun} from '../../lib/cpp-host.mjs';

function definition(source, signature) {
  const start = source.indexOf(signature);
  assert.ok(start >= 0, signature);
  let end = source.indexOf('{', start) + 1;
  let depth = 1;
  while (depth) {
    if (source[end] === '{') depth++;
    if (source[end] === '}') depth--;
    end++;
    assert.ok(end < source.length, signature);
  }
  return source.slice(start, end);
}
const strip = (text) => text.replace(/^#(?:include|pragma).*$/gm, '');

const tileConfig = readRepoFile('src/tiles/config/tile_config.h');
const handler = readRepoFile('src/types/scene/web_handler.cpp');
const tiles = readRepoFile('src/web/server/handlers/web_admin_tiles.cpp');

const harness = String.raw`
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>

struct String : std::string {
  using std::string::string;
  String() = default;
  String(const std::string& value) : std::string(value) {}
  size_t length() const { return size(); }
  bool startsWith(const char* prefix) const { return rfind(prefix, 0) == 0; }
};
${strip(readRepoFile('src/types/scene/scene_target.h'))}
struct Tile {
  String sensor_entity, scene_alias;
  uint8_t sensor_decimals = 1, sensor_value_font = 1, sensor_display_mode = 1;
  int32_t sensor_gauge_min = 5, sensor_gauge_max = 50;
};
${definition(tileConfig, 'static inline const String& sceneTarget(')}
${definition(tileConfig, 'static inline void setSceneTarget(')}
// HaBridgeConfig::findSceneEntity: an entity id names itself, an alias goes
// through the Bridge's list (scene_alias_text).
struct Bridge {
  std::map<std::string, String> list;
  String findSceneEntity(const String& alias) const {
    if (sceneTargetIsEntityId(alias)) return alias;
    const auto found = list.find(alias);
    return found == list.end() ? String() : found->second;
  }
} haBridgeConfig;
struct WebServer {
  std::map<std::string, String> args;
  bool hasArg(const char* name) const { return args.count(name) > 0; }
  String arg(const char* name) const { return args.at(name); }
};
String scene_picker_value(const Tile& tile);
${strip(handler)}

static void save(const char* key, const char* value) {
  WebServer server;
  server.args["scene_alias"] = value;
  Tile tile;
  apply_scene_fields_from_request(server, tile);
  std::printf("%s|%s|%s\n", key, tile.sensor_entity.c_str(), tile.scene_alias.c_str());
}

int main() {
  haBridgeConfig.list["roborock_qrevo_maxv_xian_sao_hou_tuo"] = "button.roborock_qrevo_maxv_xian_sao_hou_tuo";
  haBridgeConfig.list["movie"] = "scene.movie";
  save("alias", "roborock_qrevo_maxv_xian_sao_hou_tuo");
  save("entity", "button.roborock_qrevo_maxv_xian_sao_hou_tuo");
  save("short", "movie");
  save("unknown", "old_alias");
  Tile before;
  before.scene_alias = "movie";
  std::printf("picker|%s\n", scene_picker_value(before).c_str());
  before.scene_alias = "old_alias";
  std::printf("kept|%s\n", scene_picker_value(before).c_str());
  return 0;
}
`;

const output = compileAndRun({label: 'Button tile long entity', harness});
if (output !== null) {
  const rows = Object.fromEntries(output.trim().split(/\r?\n/).map((line) => {
    const [key, ...rest] = line.trim().split('|');
    return [key, rest];
  }));
  assert.deepEqual(rows.alias, ['button.roborock_qrevo_maxv_xian_sao_hou_tuo', ''],
    'a Bridge alias is stored as its whole entity id, not cut in scene_alias');
  assert.deepEqual(rows.entity, ['button.roborock_qrevo_maxv_xian_sao_hou_tuo', '']);
  assert.deepEqual(rows.short, ['scene.movie', '']);
  assert.deepEqual(rows.unknown, ['', 'old_alias'], 'an alias the Bridge does not know stays as it was');
  assert.deepEqual(rows.picker, ['scene.movie'], 'an alias tile from before shows its entity in the editor');
  assert.deepEqual(rows.kept, ['old_alias']);
}

// The picker's scene list offers entity ids; the editor reads the tile's
// target through scene_picker_value.
assert.match(tiles, /parseSceneList\(ha\.scene_alias_text\)\) \{\s*appendEntry\(scene\.entity,/,
  'the Button picker list offers entity ids, not Bridge aliases');
assert.doesNotMatch(tiles, /appendEntry\(scene\.alias,/);
assert.match(tiles, /tile\.type == TILE_SCENE \? scene_picker_value\(tile\)/);

// The entity id keeps its characters: 63 in the packed field, longer ones in
// the entity sidecar like every entity tile.
const config = readRepoFile('src/tiles/config/tile_config.cpp');
const entityMax = Number(/static constexpr size_t ENTITY_MAX\s*=\s*(\d+);/.exec(config)[1]);
assert.ok('button.roborock_qrevo_maxv_xian_sao_hou_tuo'.length < entityMax);
assert.match(readRepoFile('src/types/tile_type_policy.h'),
  /entityTileStoresSensorEntity\(TileType type\) \{[^}]*TILE_SCENE/);

console.log('Button tile: a picked scene, script or button keeps its whole entity id (#138)');
