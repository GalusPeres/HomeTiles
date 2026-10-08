// The first network transport after boot must not reconfigure MQTT or the
// direct link. The reconfigure recreates sockets after a route switch; at
// boot there is none to rebind, and it raced the worker's first connection
// (V2 b300 log: "[Link] Connected to the Bridge" -> "[MQTT] Disconnect for
// reconfiguration" -> connected again, about 1 s lost). Later transport
// changes (Ethernet <-> Wi-Fi, a lost and returning link) still reconfigure.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const manager = fs.readFileSync(path.join(root, 'src/network/network_manager.cpp'), 'utf8').replace(/\r\n/g, '\n');
const update = cppFunctionDefinitions(manager).find(f => f.name === 'HomeTilesNetworkManager::update').source;

const changed = update.slice(update.indexOf('if (transport_changed) {'));
assert.ok(changed.length > 0, 'the transport change branch exists');
const first = changed.indexOf('const bool first_transport = transport_generation_seen == 0;');
const seen = changed.indexOf('transport_generation_seen = current_generation;');
assert.ok(first >= 0 && first < seen, 'the first transport is known before the generation is taken over');
assert.match(changed, /if \(mqtt_enabled && !first_transport\) \{\s*mqtt_reconfig_requested = true;\s*\}/,
  'only a later transport change recreates the MQTT or link socket');
// Web Admin and mDNS still rebind on every change, the first one included.
assert.match(changed, /webAdminServer\.stop\(\);[\s\S]*?stopMdns\(\);[\s\S]*?was_connected = false;/);

// The boot path itself gives the client its server and callback, so the
// first connection needs no reconfigure.
const init = cppFunctionDefinitions(manager).find(f => f.name === 'HomeTilesNetworkManager::init').source;
assert.match(init, /transport_generation_seen = networkTransport\.generation\(\);/);
assert.match(init, /installMessageCallback\(\);/);

console.log('First transport: no MQTT/link reconfigure at boot, later route switches still rebind');
