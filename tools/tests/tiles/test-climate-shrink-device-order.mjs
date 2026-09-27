// The device places Climate mini tiles exactly like the Web Admin preview.
// Reported: a 2x1.5 tile with Current and a target on top and a wide Cool
// target below (item 0) showed only the Cool target on the device after
// shrinking to 2x1, while the Web Admin kept Current and the target. The
// device clamped the stored position into the smaller grid before ordering,
// so the item from the vanished row moved up and won through its lower item
// number. This test compiles the real firmware placement on the host and
// compares it with the delivered Web preview.
import assert from 'node:assert/strict';
import {mkdtempSync, rmSync, writeFileSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {spawnSync} from 'node:child_process';
import vm from 'node:vm';
import {extractDeliveredFunction, readRepoFile} from '../../lib/admin-source.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const CONTENT = {AUTO: 0, EMPTY: 1, CURRENT_TEMPERATURE: 2, CURRENT_HUMIDITY: 3,
  TARGET_TEMPERATURE: 4, TARGET_TEMPERATURE_LOW: 5, TARGET_TEMPERATURE_HIGH: 6,
  TARGET_HUMIDITY: 7, HVAC_MODE: 8};
const context = vm.createContext({
  GRID_COLS: 7, GRID_ROWS: 5,
  CLIMATE_TILE_CONTENT: CONTENT,
  CLIMATE_TARGET_LAYOUT: {AUTO: 0, HORIZONTAL: 1, VERTICAL: 2},
  CLIMATE_SUPPORTED_FEATURE: {TARGET_TEMPERATURE: 1, TARGET_TEMPERATURE_RANGE: 2,
    TARGET_HUMIDITY: 4},
  CLIMATE_LAYOUT_MAGIC: 0x434c0000, CLIMATE_LAYOUT_MAGIC_MASK: 0xffff0000,
  CLIMATE_LAYOUT_VALUE_MASK: 0x00000fff, CLIMATE_GEOMETRY_PREFIX: 'CLG2:',
  CLIMATE_I18N: {heat: 'Heat', cool: 'Cool', humidityCaption: 'Humidity',
    emptyField: 'Empty', genericClimate: 'Climate', targetTemperature: 'Target'},
  escapeHtml: value => String(value)
});
vm.runInContext([
  'climateMaxGridColumns', 'climateMaxOuterRows', 'climateGridDimensions',
  'climateSlotCapacity', 'clampClimateGeometryItem', 'sanitizeClimateGeometryItem',
  'defaultClimateGeometry', 'decodeClimateGeometry', 'encodeClimateGeometry',
  'decodeClimateSlotConfig', 'getClimateLayoutPayload', 'decodeClimateTargetLayouts',
  'climateGeometryOverlaps', 'climatePlacementOrderFor', 'climateTargetCaption',
  'climateModeText', 'climateFeatureSupported', 'climateTargetInteractive',
  'climatePreviewSlots'
].map(extractDeliveredFunction).join('\n'), context);

const webState = {valid: true, available: true, unit: '°C', mode: 'cool', action: '',
  current: '20.5', currentHumidity: '45', target: '23.5', targetLow: null,
  targetHigh: null, targetHumidity: '50', supportedFeatures: 1 | 4};

// Placed items as "kind col row spanW spanH", sorted.
function webPlacement({spanW, spanH, slots, geometry}) {
  const html = context.climatePreviewSlots(
    webState, spanW, spanH, slots, [0, 0, 0, 0, 0, 0], geometry);
  return [...html.matchAll(
    /data-climate-preview-item="(\d)" style="grid-column:(\d+) \/ span (\d+);grid-row:(\d+) \/ span (\d+)/g)]
    .map(([, item, col, spanCols, row, spanRows]) =>
      [slots[item], col - 1, row - 1, spanCols, spanRows].join(' '))
    .sort();
}

// Deterministic sweep: explicit contents (kinds are then known on both
// sides), empty items and stored positions from larger tiles.
let seed = 0x2f6e2b1;
const random = limit => {
  seed = (Math.imul(seed, 1103515245) + 12345) >>> 0;
  return (seed >>> 8) % limit;
};
const kinds = [CONTENT.EMPTY, CONTENT.CURRENT_TEMPERATURE, CONTENT.CURRENT_HUMIDITY,
  CONTENT.TARGET_TEMPERATURE, CONTENT.TARGET_HUMIDITY, CONTENT.HVAC_MODE];
const heights = [1, 1.5, 2, 2.5, 3];
const reported = {
  spanW: 2, spanH: 1,
  slots: [CONTENT.TARGET_TEMPERATURE, CONTENT.CURRENT_TEMPERATURE,
    CONTENT.TARGET_TEMPERATURE, CONTENT.EMPTY, CONTENT.EMPTY, CONTENT.EMPTY],
  geometry: context.encodeClimateGeometry([
    {col: 0, row: 1, spanW: 2, spanH: 1}, {col: 0, row: 0, spanW: 1, spanH: 1},
    {col: 1, row: 0, spanW: 1, spanH: 1}, {col: 0, row: 0, spanW: 1, spanH: 1},
    {col: 0, row: 0, spanW: 1, spanH: 1}, {col: 0, row: 0, spanW: 1, spanH: 1}])
};
const cases = [reported, {...reported, spanH: 1.5}];
for (let index = 0; index < 400; ++index) {
  cases.push({
    spanW: 1 + random(4),
    spanH: heights[random(heights.length)],
    slots: Array.from({length: 6}, () => kinds[random(kinds.length)]),
    geometry: context.encodeClimateGeometry(Array.from({length: 6}, () => ({
      col: random(4), row: random(5), spanW: 1 + random(2), spanH: 1 + random(2)})))
  });
}

assert.deepEqual(webPlacement(reported), ['2 0 0 1 1', '4 1 0 1 1'],
  'Web Admin keeps Current and the target on top at 2x1');

// Host build of the real firmware functions.
const header = readRepoFile('src/tiles/config/tile_config.h').replace(/\r\n?/g, '\n');
const headerStart = header.indexOf('enum ClimateTileContent : uint8_t {');
const geometryFn = cppFunctionDefinitions(header)
  .find(item => item.name === 'getClimateTileItemGeometry');
assert.ok(headerStart > 0 && geometryFn, 'Climate section of tile_config.h found');
const renderer = readRepoFile('src/types/climate/renderer.cpp').replace(/\r\n?/g, '\n');
const rendererFn = name => {
  const found = cppFunctionDefinitions(renderer).find(item => item.name === name);
  assert.ok(found, `${name} found in renderer.cpp`);
  return found.source;
};
const slotKindEnum = renderer.match(/enum class ClimateTileSlotKind : uint8_t \{[\s\S]*?\};/)[0];

const caseLines = cases.map(({spanW, spanH, slots, geometry}) => {
  const packed = slots.reduce((sum, value, index) => sum | (value << (index * 4)), 0) >>> 0;
  return `  {${spanW}, ${spanH.toFixed(1)}f, ${packed}u, "${geometry}"},`;
}).join('\n');

const source = String.raw`
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string>

#define GRID_COLS 7
#define GRID_ROWS 5

struct String {
  std::string text;
  String(const char* value = "") : text(value ? value : "") {}
  bool startsWith(const char* prefix) const { return text.rfind(prefix, 0) == 0; }
  size_t length() const { return text.size(); }
  char operator[](size_t index) const { return index < text.size() ? text[index] : 0; }
  bool equalsIgnoreCase(const char* other) const {
    std::string a = text, b = other;
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
      if (std::tolower(static_cast<unsigned char>(a[i])) !=
          std::tolower(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
  }
};

struct Tile {
  float span_w = 1;
  float span_h = 1;
  int32_t sensor_gauge_min = 0;
  int32_t sensor_gauge_max = 0;
  String scene_alias;
};

struct ClimateState {
  bool valid = true;
  bool available = true;
  bool has_current_temperature = true;
  bool has_current_humidity = true;
  bool has_target_temperature = true;
  bool has_target_humidity = true;
  bool has_target_range = false;
  char hvac_mode[16] = "cool";
};

${header.slice(headerStart, geometryFn.end)}

${slotKindEnum}

${rendererFn('slot_is_adjustable')}

${rendererFn('configured_slot_kind')}

${rendererFn('build_automatic_slot_kinds')}

${rendererFn('build_slot_kinds')}

static const uint8_t kKindToContent[] = {0, 2, 3, 4, 5, 6, 7, 8};

struct Case { int span_w; float span_h; uint32_t slots; const char* geometry; };
static const Case kCases[] = {
${caseLines}
};

int main() {
  for (const Case& test : kCases) {
    Tile tile;
    tile.span_w = static_cast<float>(test.span_w);
    tile.span_h = test.span_h;
    tile.sensor_gauge_min = static_cast<int32_t>(test.slots);
    tile.scene_alias = String(test.geometry);
    ClimateState state;
    ClimateTileSlotKind kinds[CLIMATE_TILE_MAX_CONTENT_SLOTS] = {};
    ClimateTileTargetLayout layouts[CLIMATE_TILE_MAX_CONTENT_SLOTS] = {};
    ClimateTileItemGeometry geometry[CLIMATE_TILE_MAX_CONTENT_SLOTS] = {};
    const uint8_t count = build_slot_kinds(
        tile, state, kinds, layouts, geometry, CLIMATE_TILE_MAX_CONTENT_SLOTS);
    for (uint8_t i = 0; i < count; ++i) {
      std::printf("%s%u %u %u %u %u", i ? "|" : "",
                  kKindToContent[static_cast<uint8_t>(kinds[i])],
                  geometry[i].col, geometry[i].row,
                  geometry[i].span_w, geometry[i].span_h);
    }
    std::printf("\n");
  }
  return 0;
}
`;

function findCompiler() {
  for (const candidate of [process.env.CXX, 'clang++', 'g++', 'c++'].filter(Boolean)) {
    const check = spawnSync(candidate, ['--version'], {encoding: 'utf8'});
    if (!check.error && check.status === 0) return candidate;
  }
  return null;
}

const compiler = findCompiler();
if (!compiler) {
  console.log('SKIP: no C++ compiler for the Climate device placement test');
} else {
  const tempRoot = mkdtempSync(join(tmpdir(), 'hometiles-climate-order-'));
  try {
    const sourcePath = join(tempRoot, 'climate_order.cpp');
    const outputPath = join(tempRoot,
      process.platform === 'win32' ? 'climate_order.exe' : 'climate_order');
    writeFileSync(sourcePath, source, 'utf8');
    const compile = spawnSync(compiler, ['-std=c++17', sourcePath, '-o', outputPath],
      {encoding: 'utf8'});
    assert.equal(compile.status, 0,
      `Climate placement did not compile:\n${compile.stdout}${compile.stderr}`);
    const run = spawnSync(outputPath, [], {encoding: 'utf8'});
    assert.equal(run.status, 0, `Climate placement failed:\n${run.stdout}${run.stderr}`);
    const lines = run.stdout.replace(/\r/g, '').split('\n');
    cases.forEach((test, index) => {
      const device = lines[index] ? lines[index].split('|').sort() : [];
      assert.deepEqual(device, webPlacement(test),
        `Device and Web Admin differ for ${JSON.stringify(test)}`);
    });
    console.log(`PASS Climate device placement matches the Web Admin in ${cases.length} layouts`);
  } finally {
    rmSync(tempRoot, {recursive: true, force: true});
  }
}
