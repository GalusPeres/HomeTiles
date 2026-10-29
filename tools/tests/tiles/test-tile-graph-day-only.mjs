// A graph tile shows the day. Every numeric history answer reaches the tile
// queue, also the popup's 7D (and now 6H) answers, and the tile drew those in
// place of its day until its next refresh. Compiles the renderer's real
// apply_tile_graph_history (tile_renderer.cpp) on the host with a recording
// chart: a day answer is drawn, a 6H or 7D answer of the same entity leaves
// the tile alone; an answer without "hours" (older Bridges) still counts as
// the day.
import assert from 'node:assert/strict';

import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';
import {readRepoFile} from '../../lib/admin-source.mjs';
import {compileAndRun} from '../../lib/cpp-host.mjs';

const renderer = readRepoFile('src/tiles/runtime/tile_renderer.cpp');
const fn = (name) => {
  const found = cppFunctionDefinitions(renderer).find((f) => f.name === name);
  assert.ok(found, name);
  return found.source;
};

const harness = String.raw`
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
// The renderer calls isfinite() unqualified like the Arduino core allows;
// Linux libstdc++ puts it in the global namespace only through <math.h>.
#include <math.h>
#include "src/core/json_scan.h"

// The few Arduino String members the renderer's helpers use.
struct String {
  std::string text;
  String() = default;
  String(const char* value) : text(value ? value : "") {}
  String(const std::string& value) : text(value) {}
  String operator+(const char* other) const { return String(text + other); }
  String operator+(const String& other) const { return String(text + other.text); }
  int indexOf(const String& what, int from = 0) const {
    const size_t at = text.find(what.text, static_cast<size_t>(from));
    return at == std::string::npos ? -1 : static_cast<int>(at);
  }
  int indexOf(char what, int from = 0) const {
    const size_t at = text.find(what, static_cast<size_t>(from < 0 ? 0 : from));
    return at == std::string::npos ? -1 : static_cast<int>(at);
  }
  String substring(int from, int to) const { return String(text.substr(from, to - from)); }
  void trim() {
    const size_t first = text.find_first_not_of(" \t\r\n");
    const size_t last = text.find_last_not_of(" \t\r\n");
    text = first == std::string::npos ? "" : text.substr(first, last - first + 1);
  }
  size_t length() const { return text.size(); }
  const char* c_str() const { return text.c_str(); }
  bool equalsIgnoreCase(const String& other) const {
    return text.size() == other.text.size() &&
           std::equal(text.begin(), text.end(), other.text.begin(),
                      [](char a, char b) { return std::tolower(a) == std::tolower(b); });
  }
};
struct { template <class... T> void printf(const char*, T...) {} } Serial;

// A recording chart: the points the tile was given.
typedef int32_t lv_coord_t;
constexpr int32_t LV_CHART_POINT_NONE = INT32_MAX;
constexpr int LV_CHART_AXIS_PRIMARY_Y = 0;
struct lv_obj_t { std::vector<int32_t> points; int refreshes = 0; };
struct lv_chart_series_t {};
int32_t lv_obj_get_content_width(lv_obj_t*) { return 400; }
void lv_chart_set_point_count(lv_obj_t* chart, uint16_t count) { chart->points.assign(count, LV_CHART_POINT_NONE); }
void lv_chart_set_value_by_id(lv_obj_t* chart, lv_chart_series_t*, uint16_t id, int32_t value) { chart->points[id] = value; }
void lv_chart_set_range(lv_obj_t*, int, int32_t, int32_t) {}
void lv_chart_refresh(lv_obj_t* chart) { ++chart->refreshes; }

constexpr int TILE_SENSOR = 1;
constexpr int TILES_PER_GRID = 2;
struct Tile { int type = 0; uint8_t sensor_display_mode = 0; String sensor_entity; };
struct TileGridConfig { Tile tiles[TILES_PER_GRID]; };
struct { TileGridConfig grid; const TileGridConfig& getActiveGrid() const { return grid; } } tileConfig;
struct SensorTileWidgets { lv_obj_t* chart = nullptr; lv_chart_series_t* series = nullptr; };
SensorTileWidgets sensors[TILES_PER_GRID];
SensorTileWidgets* g_tab0_sensors = sensors;

${fn('extract_json_string_field')}
${fn('extract_json_number_field')}
// The renderer finds the tile by the payload's entity, not the queued one.
#pragma GCC diagnostic ignored "-Wunused-parameter"
${fn('apply_tile_graph_history')}

int main() {
  lv_obj_t chart;
  lv_chart_series_t series;
  tileConfig.grid.tiles[0] = {TILE_SENSOR, 2, "sensor.usage"};
  sensors[0] = {&chart, &series};
  apply_tile_graph_history(nullptr, R"({"entity_id":"sensor.usage","hours":24,"period_minutes":5,"values":[1,2,3]})");
  std::printf("day %d %zu\n", chart.refreshes, chart.points.size());
  apply_tile_graph_history(nullptr, R"({"entity_id":"sensor.usage","hours":6,"period_minutes":1,"values":[7,8,9,10]})");
  apply_tile_graph_history(nullptr, R"({"entity_id":"sensor.usage","hours":168,"period_minutes":60,"values":[5,6]})");
  std::printf("other %d %zu\n", chart.refreshes, chart.points.size());
  apply_tile_graph_history(nullptr, R"({"entity_id":"sensor.usage","values":[4,5,6,7,8]})");
  std::printf("legacy %d %zu\n", chart.refreshes, chart.points.size());
  return 0;
}
`;
const output = compileAndRun({label: 'Tile graph day only', harness});
if (output !== null) {
  const rows = Object.fromEntries(output.trim().split('\n').map((line) => {
    const [key, ...rest] = line.trim().split(' ');
    return [key, rest];
  }));
  assert.deepEqual(rows.day, ['1', '3'], 'a day answer is drawn');
  assert.deepEqual(rows.other, ['1', '3'], '6H and 7D answers leave the tile\'s day alone');
  assert.deepEqual(rows.legacy, ['2', '5'], 'an answer without hours counts as the day');
}
console.log('tile graph: only the day reaches the tile, the popup\'s 6H and 7D stay in the popup');
