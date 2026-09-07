import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const tiles = fs.readFileSync(path.join(repoRoot, 'src/ui/tabs/tiles/tab_tiles_unified.cpp'), 'utf8')
  .replace(/\r\n?/g, '\n');

function extractFunction(signature) {
  const start = tiles.indexOf(signature);
  assert.ok(start >= 0, `Missing production function: ${signature}`);
  const brace = tiles.indexOf('{', start);
  let depth = 1;
  let end = brace + 1;
  for (; end < tiles.length && depth; ++end) {
    if (tiles[end] === '{') ++depth;
    if (tiles[end] === '}') --depth;
  }
  assert.equal(depth, 0, `Unclosed production function: ${signature}`);
  return tiles.slice(start, end);
}

const production = [
  'static bool get_cached_or_initial_payload(',
  'static bool is_disabled_token(',
  'static String resolve_tile_sensor_unit(',
  'static inline void enqueue_cached_tile_state(',
  'static void apply_cached_states(GridType grid_type, const TileGridConfig& config, bool include_media) {',
  'static void apply_cached_state_for_index(',
].map(extractFunction).join('\n\n');
assert.match(production,
  /enqueue_cached_tile_state\(grid_type, config\.tiles\[i\], i, include_media\)/);
assert.match(production,
  /enqueue_cached_tile_state\(grid_type, config\.tiles\[index\], index, true\)/);

// Compile the real dispatch, payload fallback, and unit resolution functions.
// Only Arduino String and the cache, bridge, and queue endpoints are replaced.
const source = `
#include "src/types/tile_type_policy.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <map>
#include <string>
#include <vector>

class String {
  std::string value;
public:
  String() = default;
  String(const char* text) : value(text) {}
  String(const std::string& text) : value(text) {}
  size_t length() const { return value.length(); }
  const char* c_str() const { return value.c_str(); }
  void trim() {
    const auto first = value.find_first_not_of(" \\t\\r\\n");
    const auto last = value.find_last_not_of(" \\t\\r\\n");
    value = first == std::string::npos ? "" : value.substr(first, last - first + 1);
  }
  void toLowerCase() {
    std::transform(value.begin(), value.end(), value.begin(),
      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  }
  bool operator==(const char* other) const { return value == other; }
};

enum class GridType : uint8_t { TAB0, TAB1, TAB2 };
constexpr uint8_t TILES_PER_GRID = 35;
struct Tile {
  TileType type = TILE_EMPTY;
  String sensor_entity;
  String sensor_unit;
};
struct TileGridConfig { std::array<Tile, TILES_PER_GRID> tiles{}; };

static std::map<std::string, String> cache, initial_values, bridge_units, energy_units;
static std::vector<std::string> events;
struct Bridge {
  String findEditableValue(const String& entity) { return initial_values[entity.c_str()]; }
  String findSensorInitialValue(const String& entity) {
    events.push_back("initial");
    return initial_values[entity.c_str()];
  }
  String findSensorUnit(const String& entity) {
    events.push_back("bridge-unit");
    return bridge_units[entity.c_str()];
  }
} haBridgeConfig;
static String energy_find_cached_unit(const String& entity) {
  events.push_back("energy-unit");
  return energy_units[entity.c_str()];
}
static bool get_cached_entity_payload(const char* entity, String& out) {
  events.push_back("cache");
  const auto found = cache.find(entity);
  if (found == cache.end()) return false;
  out = found->second;
  return true;
}
static void cache_entity_payload(const char* entity, const char* payload) {
  events.push_back("store");
  cache[entity] = payload;
}

struct Queued {
  std::string route;
  GridType grid;
  uint8_t index;
  std::string payload;
  bool null_unit;
  std::string unit;
};
static std::vector<Queued> queued;
static void record(const char* route, GridType grid, uint8_t index,
                   const char* payload, const char* unit = nullptr) {
  events.push_back("queue");
  assert(payload != nullptr);
  queued.push_back({route, grid, index, payload, unit == nullptr, unit ? unit : ""});
}
static void queue_sensor_tile_update(GridType grid, uint8_t index,
                                     const char* payload, const char* unit) {
  record("sensor", grid, index, payload, unit);
}
${['switch', 'weather', 'media', 'climate', 'cover', 'binary_sensor'].map(type =>
  `static void queue_${type}_tile_update(GridType grid, uint8_t index, const char* payload) {
  record("${type}", grid, index, payload);
}`).join('\n')}

static void refresh_editable_tile(GridType grid, uint8_t index) { record("editable", grid, index, cache["entity.state"].c_str()); }
${production}

static void clear() {
  cache.clear(); initial_values.clear(); bridge_units.clear(); energy_units.clear();
  events.clear(); queued.clear();
}

// Expected routes use the stable persisted IDs, independent of policy helpers.
static const char* expected_route(unsigned type) {
  constexpr std::array<const char*, 24> routes = {
    "", "sensor", "", "", "", "switch", "", "", "", "", "", "",
    "weather", "", "sensor", "media", "", "climate", "", "cover", "binary_sensor", "editable", "editable", "editable"
  };
  return type < routes.size() ? routes[type] : "";
}

static void expect_one(const char* route, GridType grid, uint8_t index,
                       const char* payload, const char* unit = nullptr) {
  assert(queued.size() == 1);
  const Queued& result = queued.front();
  assert(result.route == route && result.grid == grid && result.index == index);
  assert(result.payload == payload);
  assert(result.null_unit == (unit == nullptr));
  assert(result.unit == (unit ? unit : ""));
}

int main() {
  TileGridConfig config;
  for (unsigned raw = 0; raw < 256; ++raw) {
    Tile& tile = config.tiles[7];
    tile = {static_cast<TileType>(raw), "entity.state", ""};
    const char* route = expected_route(raw);
    for (bool include_media : {false, true}) {
      for (const char* payload : {"", "0", "unknown", "unavailable", "null", "{\\"state\\":\\"on\\"}"}) {
        clear();
        cache["entity.state"] = payload;
        enqueue_cached_tile_state(GridType::TAB2, tile, 7, include_media);
        if (route[0] && (include_media || raw != 15)) {
          expect_one(route, GridType::TAB2, 7, payload);
        } else {
          assert(queued.empty() && events.empty());
        }
      }
    }
    clear();
    cache["entity.state"] = "0";
    apply_cached_state_for_index(GridType::TAB1, config, 7);
    if (route[0]) expect_one(route, GridType::TAB1, 7, "0");
    else assert(queued.empty() && events.empty());

    clear();
    tile.sensor_entity = "";
    apply_cached_state_for_index(GridType::TAB0, config, 7);
    assert(queued.empty() && events.empty());
  }

  // Cache misses fall back to the bridge and populate the cache before enqueue.
  for (unsigned raw : {1u, 5u, 12u, 14u, 15u, 17u, 19u, 20u}) {
    Tile& tile = config.tiles[7];
    tile = {static_cast<TileType>(raw), "entity.state", ""};
    clear();
    apply_cached_state_for_index(GridType::TAB0, config, 7);
    assert(queued.empty());
    assert((events == std::vector<std::string>{"cache", "initial"}));

    clear();
    initial_values["entity.state"] = "0";
    apply_cached_state_for_index(GridType::TAB0, config, 7);
    expect_one(expected_route(raw), GridType::TAB0, 7, "0");
    assert(cache.at("entity.state") == "0");
    assert(events[0] == "cache" && events[1] == "initial" && events[2] == "store");
    assert(events.back() == "queue");

    clear();
    cache["entity.state"] = "unavailable";
    initial_values["entity.state"] = "123";
    apply_cached_state_for_index(GridType::TAB0, config, 7);
    expect_one(expected_route(raw), GridType::TAB0, 7, "unavailable");
    assert(std::find(events.begin(), events.end(), "initial") == events.end());
  }

  for (TileType type : {TILE_SENSOR, TILE_ENERGY}) {
    Tile& tile = config.tiles[7];
    tile = {type, "entity.state", " kWh "};
    clear(); cache["entity.state"] = "17.55";
    apply_cached_state_for_index(GridType::TAB0, config, 7);
    expect_one("sensor", GridType::TAB0, 7, "17.55", "kWh");
    assert((events == std::vector<std::string>{"cache", "queue"}));

    for (const char* disabled : {"-", "none", "NULL", "No", "off", " \\t "}) {
      clear(); cache["entity.state"] = "17.55";
      bridge_units["entity.state"] = "W"; energy_units["entity.state"] = "kWh";
      tile.sensor_unit = disabled;
      apply_cached_state_for_index(GridType::TAB0, config, 7);
      expect_one("sensor", GridType::TAB0, 7, "17.55");
      assert((events == std::vector<std::string>{"cache", "queue"}));
    }

    clear(); cache["entity.state"] = "17.55";
    tile.sensor_unit = ""; bridge_units["entity.state"] = " W ";
    energy_units["entity.state"] = "kWh";
    apply_cached_state_for_index(GridType::TAB0, config, 7);
    expect_one("sensor", GridType::TAB0, 7, "17.55", "W");
    assert((events == std::vector<std::string>{"cache", "bridge-unit", "queue"}));

    clear(); cache["entity.state"] = "17.55";
    energy_units["entity.state"] = " kWh ";
    apply_cached_state_for_index(GridType::TAB0, config, 7);
    expect_one("sensor", GridType::TAB0, 7, "17.55", type == TILE_ENERGY ? "kWh" : nullptr);
    assert((std::find(events.begin(), events.end(), "energy-unit") != events.end()) == (type == TILE_ENERGY));

    // Preserve the existing late trim: whitespace from the bridge suppresses
    // the Energy fallback, then becomes a null unit at the queue boundary.
    clear(); cache["entity.state"] = "17.55";
    bridge_units["entity.state"] = " "; energy_units["entity.state"] = "kWh";
    apply_cached_state_for_index(GridType::TAB0, config, 7);
    expect_one("sensor", GridType::TAB0, 7, "17.55");
    assert((events == std::vector<std::string>{"cache", "bridge-unit", "queue"}));
  }

  // Batch enqueue order follows tile indices; hidden grids omit only Media.
  for (bool include_media : {false, true}) {
    clear();
    for (unsigned i = 0; i < TILES_PER_GRID; ++i) {
      const std::string entity = "entity." + std::to_string(i);
      config.tiles[i] = {static_cast<TileType>(i % 21), entity, ""};
      cache[entity] = std::to_string(i);
    }
    apply_cached_states(GridType::TAB1, config, include_media);
    size_t next = 0;
    for (unsigned i = 0; i < TILES_PER_GRID; ++i) {
      const unsigned type = i % 21;
      if (!expected_route(type)[0] || (!include_media && type == 15)) continue;
      assert(next < queued.size());
      const auto& item = queued[next++];
      assert(item.route == expected_route(type) && item.index == i);
      assert(item.grid == GridType::TAB1 && item.payload == std::to_string(i));
    }
    assert(next == queued.size());
  }
  clear();
  apply_cached_state_for_index(GridType::TAB0, config, TILES_PER_GRID);
  apply_cached_state_for_index(GridType::TAB0, config, 255);
  assert(queued.empty() && events.empty());
}
`;

const compiler = [process.env.CXX, 'clang++', 'g++', 'c++'].filter(Boolean)
  .find(candidate => spawnSync(candidate, ['--version'], {encoding: 'utf8'}).status === 0);
if (!compiler) {
  console.log('SKIP: Cached tile state dispatch requires a C++17 host compiler');
  process.exit(0);
}
const buildDir = path.join(repoRoot, 'build', 'tests', 'cached-tile-state-dispatch');
fs.mkdirSync(buildDir, {recursive: true});
const sourcePath = path.join(buildDir, 'dispatch-test.cpp');
const outputPath = path.join(buildDir, process.platform === 'win32' ? 'dispatch-test.exe' : 'dispatch-test');
fs.writeFileSync(sourcePath, source);
const compiled = spawnSync(compiler,
  ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', repoRoot, sourcePath, '-o', outputPath],
  {encoding: 'utf8'});
assert.equal(compiled.status, 0, `${compiled.stdout}${compiled.stderr}`);
const run = spawnSync(outputPath, [], {encoding: 'utf8'});
assert.equal(run.status, 0, `${run.stdout}${run.stderr}`);
console.log('Cached tile state dispatch, payload fallback, units, and hidden Media: PASS');
