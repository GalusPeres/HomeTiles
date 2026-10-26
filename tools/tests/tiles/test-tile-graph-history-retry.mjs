// A Sensor graph tile built before the connection is up (at boot) asked for
// its day while offline; mqttPublishHistoryRequest drops such a request and
// nothing asked again, so the tile stayed empty until its popup fetched the
// history (user log 2026-10-26, "Usage" tile). Two answers within one loop
// pass also lost the first: the queue had one slot. Compiles the renderer's
// real queue and service (tile_renderer.cpp) on the host: the shown grid's
// graph tiles ask once the connection is up and every five minutes, never
// while offline, and every queued answer is applied, oldest first.
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
const slice = (from, to) => {
  const start = renderer.indexOf(from);
  const end = renderer.indexOf(to, start);
  assert.ok(start >= 0 && end > start, from);
  return renderer.slice(start, end + to.length);
};

const harness = String.raw`
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// The few Arduino String members the queue uses.
struct String {
  std::string text;
  String() = default;
  String(const char* value) : text(value ? value : "") {}
  String substring(size_t from, size_t to) const { return String(text.substr(from, to - from).c_str()); }
  const char* c_str() const { return text.c_str(); }
  size_t length() const { return text.size(); }
};
constexpr int TILE_SENSOR = 1;
constexpr int TILES_PER_GRID = 4;
struct Tile {
  int type = 0;
  uint8_t sensor_display_mode = 0;
  String sensor_entity;
};
struct TileGridConfig { Tile tiles[TILES_PER_GRID]; };
struct { TileGridConfig grid; const TileGridConfig& getActiveGrid() const { return grid; } } tileConfig;
struct SensorTileWidgets { void* chart = nullptr; };
SensorTileWidgets sensors[TILES_PER_GRID];
SensorTileWidgets* g_tab0_sensors = sensors;
bool online = false;
struct { bool isMqttConnected() const { return online; } } networkManager;
uint32_t now_ms = 1000;
uint32_t millis() { return now_ms; }
std::vector<std::string> requests, applied;
void request_tile_graph_history(const char* entity) { requests.push_back(entity); }
static void apply_tile_graph_history(const char*, const char* payload) { applied.push_back(payload); }

${slice('struct TileGraphHistoryUpdate {', 'static size_t g_tile_graph_next = 0;')}
${fn('queue_tile_graph_history')}
${fn('process_tile_graph_queue')}
${slice('constexpr uint32_t kTileGraphRefreshMs', 'static uint32_t g_tile_graph_asked_ms = 0;')}
${fn('tile_graph_service_periodic')}

int main() {
  int chart = 0;
  // A graph tile, a Sensor without a graph, a graph without a widget.
  tileConfig.grid.tiles[0] = {TILE_SENSOR, 2, "sensor.hauslast"};
  sensors[0].chart = &chart;
  tileConfig.grid.tiles[1] = {TILE_SENSOR, 0, "sensor.other"};
  sensors[1].chart = &chart;
  tileConfig.grid.tiles[2] = {TILE_SENSOR, 2, "sensor.hidden"};
  // Offline: nothing asks.
  tile_graph_service_periodic();
  std::printf("offline %zu\n", requests.size());
  // The connection comes up: the graph tile asks at once, then not again
  // within five minutes.
  online = true;
  tile_graph_service_periodic();
  now_ms += 60000;
  tile_graph_service_periodic();
  std::printf("online %zu %s\n", requests.size(), requests.empty() ? "" : requests[0].c_str());
  // Five minutes after: again.
  now_ms += 4 * 60000;
  tile_graph_service_periodic();
  std::printf("refresh %zu\n", requests.size());
  // A reconnect asks at once.
  online = false;
  tile_graph_service_periodic();
  online = true;
  tile_graph_service_periodic();
  std::printf("reconnect %zu\n", requests.size());
  // Two answers in one loop pass: both applied, oldest first; then empty.
  queue_tile_graph_history("sensor.a", "first", 5);
  queue_tile_graph_history("sensor.b", "second", 6);
  process_tile_graph_queue();
  process_tile_graph_queue();
  std::printf("applied %zu %s %s\n", applied.size(), applied.size() > 0 ? applied[0].c_str() : "",
              applied.size() > 1 ? applied[1].c_str() : "");
  // More answers than slots: the newest four stay, oldest first.
  applied.clear();
  const char* answers[] = {"1", "2", "3", "4", "5"};
  for (const char* answer : answers) queue_tile_graph_history(nullptr, answer, 1);
  process_tile_graph_queue();
  std::printf("bounded %zu %s %s\n", applied.size(), applied.size() > 0 ? applied.front().c_str() : "",
              applied.size() > 0 ? applied.back().c_str() : "");
  return 0;
}
`;
const output = compileAndRun({label: 'Tile graph history retry', harness});
if (output !== null) {
  const rows = Object.fromEntries(output.trim().split('\n').map((line) => {
    const [key, ...rest] = line.trim().split(' ');
    return [key, rest];
  }));
  assert.deepEqual(rows.offline, ['0'], 'offline: no request');
  assert.deepEqual(rows.online, ['1', 'sensor.hauslast'], 'online: the graph tile asks at once, once');
  assert.deepEqual(rows.refresh, ['2'], 'five minutes after: again');
  assert.deepEqual(rows.reconnect, ['3'], 'a reconnect asks at once');
  assert.deepEqual(rows.applied, ['2', 'first', 'second'], 'two answers in one pass: both, oldest first');
  assert.deepEqual(rows.bounded, ['4', '2', '5'], 'bounded queue: the newest four, oldest first');
}
console.log('tile graph history: asks after connecting and every five minutes, every queued answer applied');
