// Wiring of the direct Bridge link (docs-dev/bridge-link.md) into the
// firmware: the network worker keeps its single-owner model and only swaps the
// client, pairing hands its key to the link, a panel without a key connects
// only to pair, and the Bridge's address arrives through one guarded endpoint.
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';

const header = readRepoFile('src/network/network_manager.h');
assert.match(header, /BridgeTransportClient mqtt_client;/, 'the worker owns one client for MQTT or the link');
assert.match(header, /portMUX_TYPE link_mux_ = portMUX_INITIALIZER_UNLOCKED;/,
  'link credentials cross from the loop task to the worker under a lock');

const network = readRepoFile('src/network/network_manager.cpp');
const slice = (from, to) => {
  const start = network.indexOf(from);
  assert.ok(start >= 0, `missing ${from}`);
  const end = network.indexOf(to, start + from.length);
  assert.ok(end > start, `missing ${to} after ${from}`);
  return network.slice(start, end);
};
const init = slice('void HomeTilesNetworkManager::init() {', 'void HomeTilesNetworkManager::connectWifi()');
assert.match(init, /link_configured_ = link_config::configured\(\);[\s\S]*mqtt_client\.useLink\(true\);/);
assert.match(init, /mqtt_enabled = link_configured_ \|\| configManager\.hasMqttConfig\(\);/,
  'a stored Bridge address replaces MQTT');

const connect = slice('void HomeTilesNetworkManager::connectMqtt() {', 'bool HomeTilesNetworkManager::connectMqttBroker(');
assert.match(connect, /if \(mqtt_client\.linkMode\(\)\) \{\s*if \(!bridgeConnectionWanted\(\)\) \{[\s\S]*?return;\s*\}\s*ok = connectLink\(\);/,
  'an unpaired panel without a pairing request does not connect');
assert.match(connect, /mqtt_retry_at = millis\(\) \+ \(3000UL << shift\);/, 'both transports keep the backoff');
assert.match(connect, /finishMqttConnect\(stat_topic\);/, 'both transports share the post-connect steps');

const link = slice('bool HomeTilesNetworkManager::connectLink() {', 'void HomeTilesNetworkManager::finishMqttConnect(');
assert.match(link, /portENTER_CRITICAL\(&link_mux_\);\s*credentials = link_credentials_;/);
assert.match(link, /ht_crypto::secureZero\(&credentials, sizeof\(credentials\)\);/, 'the worker wipes its key copy');

const worker = slice('void HomeTilesNetworkManager::serviceMqttWorker() {', 'void HomeTilesNetworkManager::drainOutboundQueues(');
const changed = worker.slice(worker.indexOf('if (link_changed_ && mqtt_client.linkMode())'));
assert.ok(changed.indexOf('drainOutboundQueues(') < changed.indexOf('mqtt_client.disconnect()'),
  'a queued unpair or pairing abort leaves before the link reconnects');

assert.match(network, /if \(configManager\.hasMqttConfig\(\) \|\| link_configured_\) \{\s*stopMdns\(\);/,
  'mDNS stays off once the panel has a Bridge');
assert.match(network, /const char\* key_link = "link";\s*const char\* val_link = "1";/,
  'mDNS tells the Bridge that this firmware can be added without MQTT');
const setPairing = slice('void HomeTilesNetworkManager::setLinkPairing(', 'void HomeTilesNetworkManager::requestLinkPairing(');
assert.match(setPairing, /ht_crypto::secureZero\(link_credentials_\.key, sizeof\(link_credentials_\.key\)\);/);

const handlers = readRepoFile('src/network/mqtt/mqtt_handlers.cpp');
assert.match(handlers, /if \(networkManager\.linkPairMode\(\)\) \{[\s\S]{0,200}command_channel::onMqttConnected\(\);\s*return;\s*\}/,
  'pair mode skips subscriptions, discovery and snapshots');

const channel = readRepoFile('src/network/secure/command_channel.cpp');
const begin = channel.slice(channel.indexOf('void begin() {'), channel.indexOf('PairingState state() {'));
assert.ok(begin.indexOf('networkManager.setLinkPairing(key') > 0 &&
  begin.indexOf('networkManager.setLinkPairing(key') < begin.lastIndexOf('ht_crypto::secureZero(key'),
  'the stored key reaches the link before it is wiped');
const complete = channel.slice(channel.indexOf('void completePairing() {'), channel.indexOf('void handlePairMessage('));
assert.ok(complete.indexOf('networkManager.setLinkPairing(g_attempt->key') < complete.indexOf('wipeAttemptSecrets()'),
  'a new pairing key reaches the link before the attempt is wiped');
const turnOff = channel.slice(channel.indexOf('bool turnOff('), channel.indexOf('void handleUnpair('));
assert.ok(turnOff.indexOf('sendUnpair()') < turnOff.indexOf('networkManager.setLinkPairing(nullptr, nullptr)'),
  'the unpair is queued before the link loses its key');
assert.match(channel, /void endAttempt\([^)]*\) \{[\s\S]*?linkPairingEnded\(\);\s*\}/, 'every ended attempt leaves pair mode');
assert.match(channel, /if \(networkManager\.linkConfigured\(\)\) networkManager\.requestLinkPairing\(true\);/,
  'Pair on the display connects the link in pair mode');
assert.match(channel, /networkManager\.linkPairMode\(\) && !g_state && !attemptRunning\(\) &&\s*link_config::current\(\)\.pair_requested && startPairing\(\)/,
  'after the Bridge sent its address the panel pairs by itself');

const client = readRepoFile('src/network/link/bridge_link_client.cpp');
assert.match(client, /MALLOC_CAP_SPIRAM \| MALLOC_CAP_8BIT/, 'frame buffers prefer PSRAM');
assert.match(client, /if \(pair_mode_ && !pairTopic\(topic, false\)\) return true;/, 'pair mode publishes only the pairing topic');
assert.match(client, /kIdleTimeoutMs/, 'a silent Bridge is dropped');
assert.match(client, /kPingIdleMs/, 'an idle link sends pings');
for (const match of client.matchAll(/Serial\.printf?\(([^;]*)\);/g)) {
  const argumentsOnly = match[1].replace(/"(?:\\.|[^"\\])*"/g, '""');
  assert.doesNotMatch(argumentsOnly, /key|nonce|tx_|rx_/, `link keys and nonces are never logged: ${match[1]}`);
}

const routes = readRepoFile('src/web/server/web_admin.cpp');
assert.match(routes, /server\.on\("\/api\/link", HTTP_POST,\s*guarded\(withStorageHold\(\[this\]\(\) \{ this->handleLinkSetup\(\); \}\)\)\);/,
  'the Bridge address needs the Web Admin session when a password is set');
const web = readRepoFile('src/web/server/handlers/web_admin_handlers.cpp');
const setup = web.slice(web.indexOf('void WebAdminServer::handleLinkSetup() {'), web.indexOf('void WebAdminServer::handleRestart() {'));
assert.match(setup, /link_config::validHost\(host\.c_str\(\)\)/);
assert.match(setup, /settings\.pair_requested = true;/);
assert.ok(setup.indexOf('link_config::save(settings)') < setup.indexOf('BoardHAL::restart()'));

const sketch = readRepoFile('HomeTiles.ino');
assert.match(sketch, /command_channel::begin\(\);[\s\S]{0,200}command_channel::setPairingPromptCallback\(settings_show_pairing\);/);

console.log('Direct Bridge link wiring: PASS');
