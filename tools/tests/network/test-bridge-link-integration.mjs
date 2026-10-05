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

assert.match(network, /const bool discoverable =\s*pairing_advertised_ \|\| \(!configManager\.hasMqttConfig\(\) && !link_configured_\);/,
  'mDNS stays off once the panel has a Bridge, unless Pair was pressed');
assert.match(network, /\{"pair", "1"\},\s*\};[\s\S]{0,200}?\(pairing_advertised_ \? 0u : 1u\)/,
  'Pair announces the panel to Home Assistant with TXT pair=1');
assert.match(network, /\{"link", "1"\},/,
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
const forget = channel.slice(channel.indexOf('void forgetLink(const char* why) {'), channel.indexOf('bool turnOff('));
assert.ok(forget.indexOf('link_config::clear()') < forget.indexOf('networkManager.leaveLink()'));
assert.ok(turnOff.indexOf('networkManager.setLinkPairing(nullptr, nullptr)') < turnOff.indexOf('forgetLink("pairing removed")'),
  'without its key the panel forgets the Bridge and is a new panel again');
assert.doesNotMatch(channel, /g_restart|setRestartCallback/, 'removing the pairing does not restart the panel');
assert.match(channel, /if \(networkManager\.consumeLinkForgotten\(\)\) \{[\s\S]{0,200}?if \(g_state\) \{\s*turnOff\(false, nullptr\);\s*\} else \{\s*forgetLink\("unknown to the Bridge"\);/,
  'a panel the Bridge no longer knows becomes a new panel');
assert.match(channel, /void acceptLinkSetup\(const char\* host, uint16_t port\) \{\s*closeLinkWindow\(\);[\s\S]*?networkManager\.switchToLink\(host, port\);\s*networkManager\.requestLinkPairing\(true\);/,
  'an accepted Bridge address switches to the link without a restart');
assert.match(channel, /void endAttempt\([^)]*\) \{[\s\S]*?linkPairingEnded\(\);[\s\S]*?if \(!g_state\) forgetLink\([^)]*\);\s*\}/,
  'every ended attempt leaves pair mode');
assert.match(channel, /if \(networkManager\.linkConfigured\(\)\) networkManager\.requestLinkPairing\(true\);/,
  'Pair on a linked display connects the link in pair mode');
const start = channel.slice(channel.indexOf('bool startPairing() {'), channel.indexOf('PairingPhase pairingPhase() {'));
assert.match(start, /if \(!networkManager\.linkConfigured\(\)\) \{[\s\S]*?g_link_window_until = [\s\S]*?networkManager\.setPairingAdvertised\(true\);[\s\S]*?return true;\s*\}/,
  'Pair on a panel without a link opens the two-minute window');
assert.match(channel, /constexpr uint32_t kLinkWindowMs = 120000;/);
assert.match(channel, /void endPairing\(\) \{\s*if \(g_link_window_until\) \{[\s\S]*?closeLinkWindow\(\);/,
  'Cancel closes the window');
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
assert.match(routes, /server\.on\("\/api\/link", HTTP_POST,\s*withStorageHold\(\[this\]\(\) \{ this->handleLinkSetup\(\); \}\)\);/,
  'the press on Pair replaces the Web Admin password for the Bridge address');
const web = readRepoFile('src/web/server/handlers/web_admin_handlers.cpp');
const setup = web.slice(web.indexOf('void WebAdminServer::handleLinkSetup() {'), web.indexOf('void WebAdminServer::handleRestart() {'));
assert.match(setup, /^\s*void WebAdminServer::handleLinkSetup\(\) \{\s*if \(!command_channel::linkWindowOpen\(\)\) \{\s*sendJsonError\(server, 403,/m,
  'without Pair pressed on the panel nobody on the network can redirect it');
assert.match(setup, /link_config::validHost\(host\.c_str\(\)\)/);
assert.match(setup, /settings\.pair_requested = true;/);
assert.ok(setup.indexOf('link_config::save(settings)') < setup.indexOf('if (topics_changed) {'));
assert.match(setup, /if \(topics_changed\) \{[\s\S]*?BoardHAL::restart\(\);\s*return;\s*\}[\s\S]*command_channel::acceptLinkSetup\(settings\.host, settings\.port\);/,
  'only another base topic restarts; otherwise the link starts at once');

const switchLink = slice('void HomeTilesNetworkManager::applyLinkTransport(', 'void HomeTilesNetworkManager::finishMqttConnect(');
assert.ok(switchLink.indexOf('drainOutboundQueues(') < switchLink.indexOf('mqtt_client.disconnect()'),
  'queued messages leave over the old connection before the transport changes');
assert.match(switchLink, /mqtt_client\.useLink\(true\);\s*installMessageCallback\(\);\s*mqtt_client\.setServer\(link_host_, link_port_\);\s*mqtt_enabled = true;/);
assert.match(network, /if \(request\.pending\) \{\s*applyLinkTransport\(request\);\s*return;\s*\}\s*\}\s*if \(!mqtt_enabled\) return;/,
  'the transport change runs on the worker before the MQTT gate');
const refusals = slice('bool HomeTilesNetworkManager::connectLink() {', 'void HomeTilesNetworkManager::installMessageCallback()');
assert.match(refusals, /strcmp\(mqtt_client\.link\(\)\.refuseReason\(\), "unknown"\) == 0\) \{\s*if \(\+\+link_unknown_refusals_ >= 3\)/,
  'only three refusals in a row count as a removed entry');

const sketch = readRepoFile('HomeTiles.ino');
assert.match(sketch, /command_channel::begin\(\);[\s\S]{0,200}command_channel::setPairingPromptCallback\(settings_show_pairing\);/);
assert.doesNotMatch(sketch, /setRestartCallback/);

// Moving from MQTT to the link leaves no retained identity on the broker: its
// old announcement made Home Assistant offer the panel as a new MQTT panel.
const leave = slice('void HomeTilesNetworkManager::applyLinkTransport(', 'void HomeTilesNetworkManager::finishMqttConnect(');
assert.match(leave, /drainOutboundQueues\(kMqttOutboundDrainNormal\);\s*[\s\S]*?if \(request\.enable && !mqtt_client\.linkMode\(\)\) clearMqttLeftovers\(\);\s*mqtt_client\.disconnect\(\);/,
  'queued messages go first, then the leftovers are cleared, then a clean disconnect');
const leftovers = slice('void HomeTilesNetworkManager::clearMqttLeftovers() {', 'String HomeTilesNetworkManager::bridgeConfigTopic() const {');
for (const topic of ['announcement.c_str()', 'mqttTopics.topic(TopicKey::STAT_CONN)', 'mqttTopics.topic(TopicKey::STAT_IP)'])
  assert.ok(leftovers.includes(topic), `clears ${topic}`);
assert.match(leftovers, /mqtt_client\.publish\(topic, "", true\)/, 'empty retained messages delete them');
const announce = slice('void HomeTilesNetworkManager::publishBridgeConfig() {', 'const char* HomeTilesNetworkManager::getBridgeApplyTopic()');
assert.match(announce, /const String topic = bridgeConfigTopic\(\);/, 'announcing and clearing use the same topic');

// Pair right after a finished pairing shows the window, not the overview.
const pairStart = channel.slice(channel.indexOf('bool startPairing() {'), channel.indexOf('PairingPhase pairingPhase() {'));
assert.match(pairStart, /if \(!networkManager\.linkConfigured\(\)\) \{[\s\S]*?releaseAttempt\(\);\s*const uint32_t until = millis\(\) \+ kLinkWindowMs;/,
  'a kept, finished attempt no longer hides the pair window');

// Pair changes one TXT entry of the running advertisement. A restart per change
// (goodbye, probe, announce) lost the second of two quick changes: Unpair,
// then Pair at once, and Home Assistant never saw pair=1.
const mdnsLoop = slice('const bool discoverable =', 'if (discoverable && webAdminServer.isRunning()) {');
assert.match(mdnsLoop, /if \(mdns_active && !discoverable\) \{\s*stopMdns\(\);\s*\} else if \(mdns_active && mdns_pair_flag_ != pairing_advertised_\) \{\s*updateMdnsPairFlag\(\);\s*\}/,
  'only leaving discovery stops mDNS; the pair flag is updated in place');
const pairFlag = slice('void HomeTilesNetworkManager::updateMdnsPairFlag() {', '// ========== Direct Bridge link ==========');
assert.match(pairFlag, /\+\+mdns_txt_seq_;\s*if \(!applyMdnsTxt\(\)\) \{[\s\S]*?stopMdns\(\);\s*return;\s*\}/,
  'every change gets a new seq; a failed update falls back to a restart with the current flag');
// Home Assistant's mDNS cache ignores a TXT record equal to one it still holds
// (up to 10 s after it was replaced): pair on, off, on within seconds showed no
// card, or kept a stale one. seq makes every TXT set unique.
const txtSet = slice('bool HomeTilesNetworkManager::applyMdnsTxt() {', 'void HomeTilesNetworkManager::updateMdnsPairFlag() {');
assert.match(txtSet, /\{"link", "1"\},\s*\{"seq", seq\},[\s\S]*?\{"pair", "1"\},\s*\};/, 'pair is the last entry, after seq');
assert.match(txtSet, /sizeof\(txt\) \/ sizeof\(txt\[0\]\) - \(pairing_advertised_ \? 0u : 1u\)/, 'pair is left out unless Pair was pressed');
assert.match(txtSet, /if \(mdns_service_txt_set\("_hometiles", "_tcp", txt, count\) != ESP_OK\) return false;\s*mdns_pair_flag_ = pairing_advertised_;/,
  'the whole set is replaced at once, so one announcement carries it');
const mdnsStart = slice('void HomeTilesNetworkManager::startMdns() {', 'bool HomeTilesNetworkManager::applyMdnsTxt() {');
assert.match(mdnsStart, /if \(!mdns_txt_seq_\) mdns_txt_seq_ = esp_random\(\) \| 1u;\s*if \(!applyMdnsTxt\(\)\)/,
  'seq starts randomly per boot, so no TXT set from before a restart repeats');
assert.doesNotMatch(mdnsStart, /addServiceTxt/, 'the TXT set comes from applyMdnsTxt() only');
assert.match(network, /MDNS\.addService\("hometiles", "tcp", 80\);/,
  'the Arduino wrapper registers the service as _hometiles._tcp');

// Rejected, cancelled or timed out after Submit: the panel must not keep the
// address without a key, or the next Pair tries the link and a new panel hangs.
const endAttemptFn = channel.slice(channel.indexOf('void endAttempt(PairingPhase phase, const char* reason) {'), channel.indexOf('void completePairing() {'));
assert.match(endAttemptFn, /linkPairingEnded\(\);[\s\S]*?if \(!g_state\) forgetLink\("pairing ended without a key"\);/,
  'a link pairing that ends without a key makes the panel new again');
const cancel = channel.slice(channel.indexOf('void endPairing() {'), channel.indexOf('bool disable(bool* bridge_notified) {'));
assert.match(cancel, /if \(!g_attempt\) \{[\s\S]*?if \(g_link_setup_until\) \{\s*g_link_setup_until = 0;[\s\S]*?if \(!g_state\) forgetLink\("setup cancelled"\);\s*\}\s*return;\s*\}/,
  'Cancel right after Submit, before the number, does not let the link pair by itself');

// Broker credentials never outlive the broker host.
const mqttSave = web.slice(web.indexOf('void WebAdminServer::handleSaveMQTT() {'), web.indexOf('if (server.hasArg("mqtt_client_id"))'));
assert.match(mqttSave, /copyIfNonEmpty\(cfg\.mqtt_pass, sizeof\(cfg\.mqtt_pass\), "mqtt_pass"\);\s*[\s\S]*?if \(!cfg\.mqtt_host\[0\]\) \{\s*cfg\.mqtt_user\[0\] = '\\0';\s*cfg\.mqtt_pass\[0\] = '\\0';\s*\}/,
  'an empty host drops the hidden password, which an empty field cannot clear');
assert.match(setup, /if \(topics_changed \|\| cfg\.mqtt_host\[0\] \|\| cfg\.mqtt_user\[0\] \|\| cfg\.mqtt_pass\[0\]\) \{[\s\S]*?cfg\.mqtt_host\[0\] = '\\0';\s*cfg\.mqtt_user\[0\] = '\\0';\s*cfg\.mqtt_pass\[0\] = '\\0';/,
  'the link setup drops host, user and password together');

console.log('Direct Bridge link wiring: PASS');
