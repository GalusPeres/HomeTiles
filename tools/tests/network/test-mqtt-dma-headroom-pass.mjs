// The MQTT worker runs every 2 ms. Its per-pass DMA pressure check must not
// walk the internal heap on every pass: heap_caps_get_largest_free_block()
// holds the heap lock with interrupts masked on the worker core, and both
// interrupt watchdog dumps from Web Admin saves during a camera stream
// (2026-09-24) showed that walk while the other core waited for the flash
// IPC. The gates in front of large transmissions keep their fresh walk.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = relative => fs.readFileSync(path.join(root, relative), 'utf8').replaceAll('\r\n', '\n');
const source = read('src/network/network_manager.cpp');
const ino = read('HomeTiles.ino');

const bodyOf = signature => {
  const start = source.indexOf(signature);
  assert.ok(start >= 0, `${signature} must exist`);
  let depth = 0;
  for (let i = source.indexOf('{', start); i < source.length; ++i) {
    if (source[i] === '{') ++depth;
    if (source[i] === '}' && --depth === 0) return source.slice(start, i + 1);
  }
  throw new Error(`${signature} is not closed`);
};

// The worker cadence the interval is measured against.
assert.match(ino, /networkManager\.serviceMqttWorker\(\);\s*vTaskDelay\(pdMS_TO_TICKS\(2\)\);/);

// P4 only, 100 ms between walks, cached result in between.
const guarded = source.slice(source.indexOf('#if defined(CONFIG_IDF_TARGET_ESP32P4)\nstatic constexpr uint32_t kMqttDmaPassCheckIntervalMs'));
assert.ok(guarded.length > 0, 'The per-pass cache is P4 only');
assert.match(source, /static constexpr uint32_t kMqttDmaPassCheckIntervalMs = 100;/);
const perPass = bodyOf('static size_t serviceMqttDmaHeadroomPerPass(uint32_t now_ms)');
assert.match(perPass,
  /if \(g_mqtt_dma_pass_checked_ms != 0 &&\s*static_cast<uint32_t>\(now_ms - g_mqtt_dma_pass_checked_ms\) < kMqttDmaPassCheckIntervalMs\) \{\s*return g_mqtt_dma_pass_largest;\s*\}/);
assert.match(perPass, /g_mqtt_dma_pass_checked_ms = now_ms != 0 \? now_ms : 1;\s*g_mqtt_dma_pass_largest = serviceMqttDmaHeadroom\(now_ms\);/);
assert.doesNotMatch(perPass, /heap_caps_/, 'The cached path never touches the heap');

// Behaviour of the throttle: at 2 ms passes over one second, 10 walks.
{
  let checked = 0;
  let walks = 0;
  const pass = now => {
    if (checked !== 0 && ((now - checked) >>> 0) < 100) return;
    checked = now !== 0 ? now : 1;
    ++walks;
  };
  for (let now = 1000; now < 2000; now += 2) pass(now);
  assert.equal(walks, 10);
  // millis() wrap-around keeps the interval.
  checked = 0;
  walks = 0;
  for (let now = 0xffffff00; now < 0xffffff00 + 400; now += 2) pass(now >>> 0);
  assert.equal(walks, 4);
}

// The worker pass uses the cached check exactly once; the large-request gate
// still walks the heap right before it grows the receive buffer.
const drain = bodyOf('void HomeTilesNetworkManager::drainOutboundQueues(uint8_t max_commands)');
assert.equal((drain.match(/serviceMqttDmaHeadroomPerPass\(/g) || []).length, 1);
assert.match(drain, /dma_largest = serviceMqttDmaHeadroomPerPass\(now_ms\);/);
assert.match(drain, /dma_largest = serviceMqttDmaHeadroom\(now_ms\);\s*if \(dma_largest < kMqttMinDmaLargestBeforeTx\) \{/);
const stream = bodyOf('void HomeTilesNetworkManager::serviceStreamPublish(uint32_t now_ms)');
assert.match(stream, /if \(serviceMqttDmaHeadroom\(now_ms\) < kMqttMinDmaLargestBeforeTx\) return;/);
assert.match(stream, /while \(serviceMqttDmaHeadroom\(millis\(\)\) < kMqttMinDmaLargestBeforeTx\)/);

console.log('MQTT DMA headroom: per-pass heap walk throttled, transmission gates fresh.');
