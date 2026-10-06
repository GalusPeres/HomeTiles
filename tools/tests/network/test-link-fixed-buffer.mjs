// The direct link receives into one fixed 64 KiB buffer. The MQTT buffer
// housekeeping kept "resizing" it on every worker pass and logged
// "[MQTT] Buffer: 65535 -> 65535 bytes (media-config, PSRAM)" hundreds of
// times while the panel waited for Home Assistant (V2 b239).
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';

const read = file => readRepoFile(file).replace(/\r\n?/g, '\n');
const client = read('src/network/link/bridge_transport_client.h');
assert.match(client, /bool resizableBuffer\(\) const \{ return !link_mode_; \}/);
assert.match(client, /bool setBufferSize\(uint16_t size\) \{ return link_mode_ \? true : mqtt_\.setBufferSize\(size\); \}/,
  'the link has nothing to resize');

const network = read('src/network/network_manager.cpp');
assert.match(network, /void HomeTilesNetworkManager::serviceBufferHousekeeping\(uint32_t now_ms\) \{\s*\/\/[^\n]*\n\s*\/\/[^\n]*\n\s*if \(!mqtt_client\.resizableBuffer\(\)\) return;/,
  'no resize attempts on the link');
const resize = network.slice(network.indexOf('bool HomeTilesNetworkManager::setMqttBufferSize('));
assert.match(resize, /if \(before == size \|\| !mqtt_client\.resizableBuffer\(\)\) \{\s*mqtt_buffer_size = before;\s*return true;\s*\}/,
  'a fixed buffer returns before the log line');
assert.ok(resize.indexOf('resizableBuffer()') < resize.indexOf('[MQTT] Buffer: %u -> %u bytes'));

console.log('Link fixed buffer: no resize attempts, no repeated buffer log');
