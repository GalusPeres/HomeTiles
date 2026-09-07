import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const sketch = fs.readFileSync(path.join(repoRoot, 'HomeTiles.ino'), 'utf8');
const service = fs.readFileSync(
  path.join(repoRoot, 'src/tiles/runtime/tile_update_service.h'), 'utf8');

// The extraction must preserve the existing scheduling decisions in loop().
const sleepStart = sketch.indexOf('if (powerManager.isInSleep())');
const activeStart = sketch.indexOf('// In idle mode, process bounded tile batches');
const activeEnd = sketch.indexOf('uint32_t t_update_queues', activeStart);
assert.ok(sleepStart >= 0 && activeStart > sleepStart && activeEnd > activeStart);
const sleep = sketch.slice(sleepStart, activeStart);
const active = sketch.slice(activeStart, activeEnd);
assert.match(sleep,
  /mqtt_process_inbound_queue\(\);[\s\S]*process_tile_update_queues<TileUpdateBudget::DrainAll>\(\);[\s\S]*settings_update_power_status\(\);/,
  'Sleep must drain live tile state after inbound MQTT and before status updates');
assert.doesNotMatch(sleep, /process_tile_graph_queue\(\);/,
  'Sleep must not start request/response graph processing');
assert.match(active,
  /if \(!camera_popup_busy\)\s*\{[\s\S]*bool idle = !powerManager\.isHighPerformance\(\);[\s\S]*if \(!idle \|\| \(millis\(\) - last_queue_ms >= 2000\)\)\s*\{[\s\S]*process_tile_update_queues<TileUpdateBudget::Active>\(\);[\s\S]*process_tile_graph_queue\(\);[\s\S]*if \(idle\) energy_service_periodic\(\);[\s\S]*last_queue_ms = millis\(\);/,
  'Camera gating, idle interval, graph order, and energy scheduling must stay in loop()');
assert.doesNotMatch(service,
  /process_tile_graph_queue|energy_service_periodic|millis\(|delay\(/,
  'The shared service must not own scheduling or request/response work');

const queueTypes = ['sensor', 'switch', 'climate', 'cover',
  'binary_sensor', 'editable', 'weather', 'media'];
const declarations = queueTypes.map(type =>
  `void ${type === "editable" ? "process_editable_updates" : `process_${type}_update_queue`}(uint8_t max_updates = 0);`).join('\n');
const endpoints = queueTypes.map((type, index) =>
  `void ${type === "editable" ? "process_editable_updates" : `process_${type}_update_queue`}(uint8_t budget) { consume(${index}, budget); }`
).join('\n');

// Compile the production header. Only the queue endpoints are replaced so the
// harness can verify ordering, bounded backlogs, and full sleep drains on a host.
const source = `
#include <array>
#include <cassert>
#include <cstddef>
#include "src/tiles/runtime/tile_update_service.h"

struct Call { std::size_t queue; uint8_t budget; };
static std::array<unsigned, 8> pending{};
static std::array<Call, 8> calls{};
static std::size_t call_count = 0;

static void consume(std::size_t queue, uint8_t budget) {
  assert(call_count < calls.size());
  calls[call_count++] = {queue, budget};
  unsigned count = pending[queue];
  if (budget != 0 && count > budget) count = budget;
  pending[queue] -= count;
}

${endpoints}

static void expect_calls(const std::array<uint8_t, 8>& budgets) {
  assert(call_count == calls.size());
  for (std::size_t i = 0; i < calls.size(); ++i) {
    assert(calls[i].queue == i);
    assert(calls[i].budget == budgets[i]);
  }
  call_count = 0;
}

int main() {
  constexpr std::array<uint8_t, 8> active = {6, 6, 4, 4, 4, 4, 4, 2};
  constexpr std::array<uint8_t, 8> drain_all = {0, 0, 0, 0, 0, 0, 0, 0};
  pending.fill(20);

  process_tile_update_queues<TileUpdateBudget::Active>();
  expect_calls(active);
  for (std::size_t i = 0; i < pending.size(); ++i) {
    assert(pending[i] == 20 - active[i]);
  }

  process_tile_update_queues<TileUpdateBudget::Active>();
  expect_calls(active);
  for (std::size_t i = 0; i < pending.size(); ++i) {
    assert(pending[i] == 20 - 2 * active[i]);
  }

  process_tile_update_queues<TileUpdateBudget::DrainAll>();
  expect_calls(drain_all);
  for (unsigned count : pending) assert(count == 0);

  // Empty queues still receive service, including Media cover housekeeping.
  process_tile_update_queues<TileUpdateBudget::Active>();
  expect_calls(active);
  for (unsigned count : pending) assert(count == 0);
  return 0;
}
`;

const compiler = [process.env.CXX, 'clang++', 'g++', 'c++']
  .filter(Boolean)
  .find(candidate => {
    const result = spawnSync(candidate, ['--version'], {encoding: 'utf8'});
    return !result.error && result.status === 0;
  });
if (!compiler) {
  console.log('SKIP: tile update service runtime test requires a C++ compiler');
  process.exit(0);
}

const buildRoot = path.join(repoRoot, 'build', 'tests');
fs.mkdirSync(buildRoot, {recursive: true});
const tempRoot = fs.mkdtempSync(path.join(buildRoot, 'tile-update-service-'));
try {
  fs.mkdirSync(path.join(tempRoot, 'src/types/value'), {recursive:true});
  fs.writeFileSync(path.join(tempRoot, 'src/types/value/value_control.h'), '#pragma once\nvoid process_editable_updates(uint8_t);\n');
  const stubDir = path.join(tempRoot, 'src', 'tiles', 'runtime');
  fs.mkdirSync(stubDir, {recursive: true});
  fs.writeFileSync(path.join(stubDir, 'tile_renderer.h'),
    `#pragma once\n#include <stdint.h>\n${declarations}\n`);
  const sourcePath = path.join(tempRoot, 'test.cpp');
  const outputPath = path.join(tempRoot,
    process.platform === 'win32' ? 'test.exe' : 'test');
  fs.writeFileSync(sourcePath, source);
  const compile = spawnSync(compiler,
    ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', tempRoot,
      '-I', repoRoot, sourcePath, '-o', outputPath], {encoding: 'utf8'});
  assert.equal(compile.status, 0,
    `Tile update service harness did not compile:\n${compile.stdout}${compile.stderr}`);
  const run = spawnSync(outputPath, [], {encoding: 'utf8'});
  assert.equal(run.status, 0,
    `Tile update service runtime contract failed:\n${run.stdout}${run.stderr}`);
  console.log('Tile update service ordering, budgets, and loop integration passed');
} finally {
  assert.ok(path.resolve(tempRoot).startsWith(path.resolve(buildRoot) + path.sep));
  fs.rmSync(tempRoot, {recursive: true, force: true});
}
