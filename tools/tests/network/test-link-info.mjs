// Issue #114 (Bridge #16): where mDNS does not reach Home Assistant (Docker
// without host networking, VLANs), the Bridge adds a panel by its IP address.
// GET /api/link answers what the panel's mDNS record says, without a password
// and without anything secret; whether Pair runs is the same check that gates
// POST /api/link. Then the setup is the one after mDNS.
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';
import {cppFunctionDefinitions} from '../../lib/cpp-source.mjs';

const read = file => readRepoFile(file).replace(/\r\n/g, '\n');
const routes = read('src/web/server/web_admin.cpp');
assert.match(routes, /server\.on\("\/api\/link", HTTP_GET, \[this\]\(\) \{ this->handleLinkInfo\(\); \}\);/,
  'GET /api/link answers without the password gate');
assert.match(routes, /server\.on\("\/api\/link", HTTP_POST,\s*withStorageHold\(\[this\]\(\) \{ this->handleLinkSetup\(\); \}\)\);/,
  'POST /api/link stays as it was');
assert.match(read('src/web/server/web_admin.h'), /void handleLinkInfo\(\);/);

const handlers = cppFunctionDefinitions(read('src/web/server/handlers/web_admin_handlers.cpp'));
const info = handlers.find(f => f.name === 'WebAdminServer::handleLinkInfo')?.source ?? assert.fail('handleLinkInfo');
const setup = handlers.find(f => f.name === 'WebAdminServer::handleLinkSetup').source;
const keys = [...info.matchAll(/\\"(\w+)\\":/g)].map(m => m[1]);
assert.deepEqual(keys, ['device_id', 'name', 'model', 'firmware', 'base_topic', 'ha_prefix', 'link', 'pair'],
  'the fields of the answer, in order');

// The same values as the mDNS record (startMdns/applyMdnsTxt), plus the firmware.
const txt = cppFunctionDefinitions(read('src/network/network_manager.cpp'))
  .find(f => f.name === 'HomeTilesNetworkManager::applyMdnsTxt').source;
const record = Object.fromEntries([...txt.matchAll(/\{"(\w+)", ([^}]+)\}/g)].map(m => [m[1], m[2].trim()]));
for (const [key, value] of Object.entries({device_id: 'did', name: 'Device::displayName()', model: 'Device::profile().key',
  base_topic: 'cfg.mqtt_base_topic', ha_prefix: 'cfg.ha_prefix', link: '"1"', pair: '"1"'})) {
  assert.equal(record[key], value, `mDNS ${key}`);
}
for (const value of ['buildDeviceId(did, sizeof(did));', 'appendJsonEscaped(json, Device::displayName());',
  'appendJsonEscaped(json, Device::profile().key);', 'appendJsonEscaped(json, FW_VERSION);',
  'appendJsonEscaped(json, cfg.mqtt_base_topic);', 'appendJsonEscaped(json, cfg.ha_prefix);',
  '"\\",\\"link\\":1,\\"pair\\":"']) {
  assert.ok(info.includes(value), `GET /api/link: ${value}`);
}
// Pair: the check that gates POST /api/link.
assert.match(info, /const bool pair = command_channel::linkWindowOpen\(\);/);
assert.match(setup, /if \(!command_channel::linkWindowOpen\(\)\) \{\s*sendJsonError\(server, 403, "Press Pair on the panel first"\);/);
// Nothing secret, nothing written.
assert.doesNotMatch(info, /pass|user|password|key_id|pairing_key|auth|token|save\(|link_config::/i,
  'no credentials, no pairing key, no change');
assert.match(info, /server\.sendHeader\("Cache-Control", "no-store"\);\s*server\.send\(200, "application\/json", json\);/);
// Anyone on the network may ask, so its log line is rate-limited.
assert.match(info, /now - last_log_ms >= 10000/);

const docs = read('docs-dev/bridge-link.md');
assert.match(docs, /GET \/api\/link/, 'the developer docs describe the request');

console.log('GET /api/link: the mDNS record without a password, Pair as for POST, nothing secret');
