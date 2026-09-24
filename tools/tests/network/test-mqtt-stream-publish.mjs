// The local camera streams JPEG snapshots above 64 KiB with PubSubClient's
// beginPublish/write/endPublish. Upstream PubSubClient truncated the MQTT
// remaining length to 16 bits, which silently corrupted such packets. This
// harness runs the production beginPublish/buildHeader against a mock socket
// and pins the single-owner worker contract of the streamed publish slot.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const read = relative => fs.readFileSync(path.join(root, relative), 'utf8').replaceAll('\r\n', '\n');
const clientSource = read('src/network/vendor/pubsubclient/PubSubClient.cpp');
const clientHeader = read('src/network/vendor/pubsubclient/PubSubClient.h');
const network = read('src/network/network_manager.cpp');
const networkHeader = read('src/network/network_manager.h');

// --- Source contract --------------------------------------------------------
assert.match(clientHeader, /size_t buildHeader\(uint8_t header, uint8_t\* buf, uint32_t length\);/,
  'The remaining-length builder must accept 32-bit lengths');
assert.match(clientHeader, /boolean lastPublishRetained\(\) const \{\s*return this->buffer != nullptr && \(this->buffer\[0\] & 0x01\) != 0;/,
  'The retain flag of the delivered PUBLISH must be readable inside the callback');

const callbackCount = (network.match(/mqtt_client\.setCallback\(/g) || []).length;
const guardedCallbacks = (network.match(
  /if \(dropRetainedLocalCameraCommand\(mqtt_client, topic\)\) return;\s*mqttCallback\(topic, payload, length\);/g) || []).length;
assert.equal(guardedCallbacks, callbackCount,
  'Every MQTT callback must drop retained local camera commands before queueing');

const fn = name => {
  const found = cppFunctionDefinitions(network).find(item => item.name === name);
  assert.ok(found, `${name} must exist`);
  return found.body;
};
const stream = fn('HomeTilesNetworkManager::serviceStreamPublish');
assert.match(stream, /mqtt_client\.beginPublish\(\s*g_stream_topic,[^;]*false\)/,
  'Snapshots are published QoS 0 and never retained');
assert.match(stream, /mqtt_client\.write\(data \+ offset, chunk\)/);
assert.match(stream, /mqtt_client\.endPublish\(\)/);
assert.match(stream, /vTaskDelay\(1\)/, 'Long streams must yield between chunks');
assert.match(stream, /mqtt_client\.disconnect\(\);\s*mqtt_connected_flag = false;/,
  'A short write leaves a corrupt packet and must close the connection');
assert.match(stream, /kMqttMinDmaLargestBeforeTx/,
  'The stream must respect the ESP-Hosted DMA headroom guard');
assert.match(network, /static constexpr size_t kStreamChunkBytes = (\d+);/);
assert.ok(Number(network.match(/kStreamChunkBytes = (\d+);/)[1]) <= 4096);

const worker = fn('HomeTilesNetworkManager::serviceMqttWorker');
assert.match(worker.slice(0, 600), /failPendingStreamPublish\("MQTT unavailable"\)/,
  'A pending stream must fail as soon as MQTT is unavailable');
assert.match(worker, /drainOutboundQueues\([\s\S]*serviceStreamPublish\(now_ms\);[\s\S]*mqtt_client\.loop\(\);/,
  'Only the MQTT worker writes the stream, between the queue drain and loop()');
const wait = fn('HomeTilesNetworkManager::mqttStreamPublishWait');
assert.match(wait, /compare_exchange_strong\(state, kStreamIdle\)/,
  'A stream that never started is withdrawn before the caller reuses its buffer');
assert.match(wait, /kStreamActive\) return StreamPublishResult::Pending/,
  'An active stream keeps the caller waiting; its buffer is still in use');
assert.match(wait, /if \(!withdraw_pending\) return StreamPublishResult::Pending;/,
  'Polling without withdrawal lets the caller cancel on disable or deadline');
assert.match(fn('HomeTilesNetworkManager::mqttStreamPublishCancel'),
  /uint8_t expected = kStreamPending;\s*if \(!g_stream_state\.compare_exchange_strong\(expected, kStreamIdle\)\)/,
  'Only a stream that has not started can be cancelled');
assert.match(networkHeader, /bool mqttStreamPublishSubmit\(const char\* topic, const uint8_t\* data,\s*size_t length, uint32_t start_deadline_ms = 0\);/);
assert.match(networkHeader, /bool mqttStreamPublishCancel\(\);/);

// A pending stream fails at the submitter deadline, waits behind the startup
// burst and the SDIO control-quiet window, and re-checks DMA headroom while
// writing with a bounded no-progress limit.
const beforeActive = stream.slice(0, stream.indexOf('kStreamActive'));
assert.match(beforeActive, /g_stream_start_deadline_ms[\s\S]*failPendingStreamPublish\(/);
assert.match(beforeActive, /kMqttStormWindowMs/);
assert.match(beforeActive, /g_mqtt_sdio_control_quiet_until/);
const writeLoop = stream.slice(stream.indexOf('while (ok && offset < length)'));
assert.match(writeLoop, /kStreamHeadroomCheckBytes[\s\S]*serviceMqttDmaHeadroom\([\s\S]*kStreamStallLimitMs[\s\S]*mqtt_client\.write\(/,
  'Long uploads re-check DMA headroom and stop after a bounded stall');
assert.equal(Number(network.match(/kStreamHeadroomCheckBytes = (\d+) \* 1024;/)[1]) * 1024 %
  Number(network.match(/kStreamChunkBytes = (\d+);/)[1]), 0,
  'Headroom checks must fall on chunk boundaries');

// --- Production beginPublish/buildHeader on a mock socket -------------------
const definitions = cppFunctionDefinitions(clientSource);
const extract = name => {
  const found = definitions.find(item => item.name === name);
  assert.ok(found, name);
  return found.source;
};

const compiler = ['clang++', 'g++', 'c++'].find(candidate =>
  spawnSync(candidate, ['--version']).status === 0);
if (!compiler) {
  console.log('SKIP: MQTT stream publish harness needs a host C++ compiler');
  process.exit(0);
}

const out = path.join(root, 'build/tests/mqtt-stream-publish');
fs.mkdirSync(out, {recursive: true});
const cpp = String.raw`
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
using boolean = bool;
constexpr int MQTT_MAX_HEADER_SIZE = 5;
constexpr uint8_t MQTTPUBLISH = 0x30;
static uint32_t millis() { return 1; }
struct Socket {
  std::vector<uint8_t> output;
  size_t write(const uint8_t* p, size_t n) { output.insert(output.end(), p, p + n); return n; }
};
struct PubSubClient {
  uint8_t storage[64] = {};
  uint8_t* buffer = storage;
  Socket socket;
  Socket* _client = &socket;
  uint32_t lastOutActivity = 0;
  bool is_connected = true;
  bool connected() { return is_connected; }
  uint16_t writeString(const char* string, uint8_t* buf, uint16_t pos);
  size_t buildHeader(uint8_t header, uint8_t* buf, uint32_t length);
  boolean beginPublish(const char* topic, unsigned int plength, boolean retained);
};
` + [extract('PubSubClient::writeString'), extract('PubSubClient::buildHeader'),
      extract('PubSubClient::beginPublish')].join('\n') + String.raw`
static std::vector<uint8_t> begin(unsigned int payload, bool retained, bool* ok) {
  PubSubClient client;
  *ok = client.beginPublish("a/b", payload, retained);
  return client.socket.output;
}
int main() {
  bool ok = false;
  // 131072-byte JPEG: remaining length 131077 needs three length bytes.
  std::vector<uint8_t> header = begin(131072, false, &ok);
  const std::vector<uint8_t> expected = {0x30, 0x85, 0x80, 0x08, 0x00, 0x03, 'a', '/', 'b'};
  assert(ok && header == expected);
  header = begin(131072, true, &ok);
  assert(ok && header[0] == 0x31);
  // Small payloads keep the one-byte form.
  header = begin(10, false, &ok);
  assert(ok && header.size() == 7 && header[1] == 15);
  // 70000 bytes exceeded the former 16-bit field.
  header = begin(70000, false, &ok);
  assert(ok && header[1] == (((70005u) & 127u) | 128u) && header[2] == ((((70005u) >> 7) & 127u) | 128u) &&
         header[3] == (70005u >> 14));
  // Lengths beyond the four-byte field are refused before anything is written.
  header = begin(268435455u, false, &ok);
  assert(!ok && header.empty());
  std::puts("stream header ok");
  return 0;
}
`;
const source = path.join(out, 'test.cpp');
const exe = path.join(out, process.platform === 'win32' ? 'test.exe' : 'test');
fs.writeFileSync(source, cpp);
const build = spawnSync(compiler, ['-std=c++17', '-Wall', '-Wextra', source, '-o', exe], {encoding: 'utf8'});
assert.equal(build.status, 0, `${build.stdout}${build.stderr}`);
const run = spawnSync(exe, [], {encoding: 'utf8'});
assert.equal(run.status, 0, `${run.stdout}${run.stderr}`);
console.log('MQTT stream publish header and worker contract passed.');
