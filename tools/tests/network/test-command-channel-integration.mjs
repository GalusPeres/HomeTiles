// Wiring of the encrypted Bridge command channel into the firmware: sealing
// happens in the one outbound queue path, sealed Bridge messages and blocked
// plaintext are handled before any other MQTT route, and nothing changes while
// pairing is off.
import assert from 'node:assert/strict';

import {readRepoFile} from '../../lib/admin-source.mjs';

const network = readRepoFile('src/network/network_manager.cpp');
const enqueue = network.slice(network.indexOf('static bool enqueueOutboundCmd('),
  network.indexOf('static void purgeOutboundQueue()'));
assert.match(enqueue, /command_channel::SealedPublish sealed;\s*if \(kind == MqttCmdKind::PUBLISH\) \{\s*switch \(command_channel::prepareOutbound\(topic, payload, payload_len, &sealed\)\)/);
assert.match(enqueue, /case command_channel::OutboundResult::Sealed:\s*topic = sealed\.topic\(\);\s*payload = sealed\.payload\(\);\s*payload_len = sealed\.length\(\);\s*retain = false;/);
assert.ok(enqueue.indexOf('prepareOutbound') < enqueue.indexOf('mqttAllocOutbound'),
  'commands are sealed before they are copied into the queue');

const handlers = readRepoFile('src/network/mqtt/mqtt_handlers.cpp');
const process = handlers.slice(handlers.indexOf('static void processMqttMessage(char* topic, uint8_t* payload, unsigned int length) {\n'));
const first = process.indexOf('command_channel::handleMqttMessage(topic, payload, length)');
assert.ok(first > 0 && first < process.indexOf('hardwareIo.handleMqttMessage'),
  'sealed Bridge messages are handled before every plain route');
assert.ok(process.indexOf('command_channel::blocksPlaintext(topic)') < process.indexOf('local_camera::handleMqttMessage'),
  'plain stream-token topics are blocked before the camera handlers while pairing is active');
assert.match(handlers, /local_camera::onMqttConnected\(\);\s*\/\/[^\n]*\n\s*command_channel::onMqttConnected\(\);/);

const sketch = readRepoFile('HomeTiles.ino');
assert.match(sketch, /local_camera::begin\(\);\s*\/\/[^\n]*\n\s*command_channel::begin\(\);/);
assert.equal((sketch.match(/command_channel::service\(\);/g) || []).length, 2, 'active and sleep loops service the channel');

const channel = readRepoFile('src/network/secure/command_channel.cpp');
assert.match(channel, /MALLOC_CAP_SPIRAM \| MALLOC_CAP_8BIT/, 'pairing state and buffers prefer PSRAM');
assert.match(channel, /if \(!g_state \|\| g_state->pairing != PairingState::Active \|\| !sealed\) \{\s*return OutboundResult::Plain;/,
  'without an active pairing every publish stays unchanged');
assert.match(channel, /xTaskGetCurrentTaskHandle\(\) != g_owner/, 'sealing stays on the loop task');
for (const match of channel.matchAll(/Serial\.printf?\(([^;]*)\);/g)) {
  const argumentsOnly = match[1].replace(/"(?:\\.|[^"\\])*"/g, '""');
  assert.doesNotMatch(argumentsOnly, /->code\b|panel_to_bridge|bridge_to_panel|->challenge|->session\b|header\.session/,
    `the pairing code, keys, challenges and session ids are never logged: ${match[1]}`);
}
assert.match(channel, /strcmp\(header\.name, g_state->challenge\) != 0/, 'a session must answer the current challenge');
assert.match(channel, /acceptSequence\(g_state->bridge_window, header\.seq\)/, 'Bridge data is replay-checked');
// Removing the pairing on either side turns it off on the other side too.
assert.match(channel, /case MessageType::Unpair:\s*handleUnpair\(header\);/);
const handleUnpair = channel.slice(channel.indexOf('void handleUnpair('), channel.indexOf('void handleData('));
assert.match(handleUnpair, /if \(!acceptInSession\(header\)\) return;\s*turnOff\(false, nullptr\);/,
  'a Bridge unpair counts only inside the current session and replay window');
const turnOff = channel.slice(channel.indexOf('bool turnOff('), channel.indexOf('void handleUnpair('));
assert.ok(turnOff.indexOf('writeRecord(nullptr)') < turnOff.indexOf('sendUnpair()') &&
  turnOff.indexOf('sendUnpair()') < turnOff.indexOf('releaseState()'),
  'the stored pairing is removed first, the Bridge is told while the keys still exist, then the keys go');
const sendUnpair = channel.slice(channel.indexOf('bool sendUnpair()'), channel.indexOf('bool turnOff('));
assert.match(sendUnpair, /!g_state->has_session/, 'unpair needs a session');
assert.match(sendUnpair, /header\.seq = g_state->next_seq\+\+;/, 'unpair is numbered like a command');
assert.match(channel, /bool disable\(bool\* bridge_notified\) \{\s*begin\(\);\s*return turnOff\(true, bridge_notified\);/);
assert.match(readRepoFile('src/ui/tabs/settings/tab_settings.cpp'), /command_channel::disable\(&bridge_notified\)[\s\S]{0,200}bridge_notified \? "" : tr\(\)\.security_encryption_off_hint/,
  'the hint to remove the code in the Bridge appears only when the Bridge could not be told');
assert.match(channel, /case MessageType::Rekey:[\s\S]{0,300}g_state->hello_requested = true;\s*g_state->hello_attempts = 0;/,
  'a rekey restarts the hello backoff');

// Every Bridge message that authenticates but is not used leaves a
// rate-limited trace, so a stuck pairing can be diagnosed from the panel log.
for (const line of ['Session answer for an old request ignored', 'Bridge asked for a new session',
  'Bridge message ignored (unreadable header)']) {
  const index = channel.indexOf(`Serial.println("[SecureCmd] ${line}")`);
  assert.ok(index > 0, `the panel logs "${line}"`);
  assert.match(channel.slice(Math.max(0, index - 80), index), /if \(logDue\(&g_state->last_(?:rekey_)?log_ms\)\) \{\s*$|\} else if \(logDue\(&g_state->last_log_ms\)\) \{\s*$/,
    `"${line}" is rate-limited`);
}

const camera = readRepoFile('src/video/local_camera/local_camera.cpp');
assert.match(camera, /bool handleMqttMessage\(const char\* topic, const uint8_t\* payload, size_t length\) \{\s*if \(!isCommandTopic\(topic\)\) return false;\s*handleCommandPayload\(payload, length\);\s*return true;\s*\}/);

const settings = readRepoFile('src/ui/tabs/settings/tab_settings.cpp');
assert.match(settings, /pairing == command_channel::PairingState::Pending &&\s*command_channel::displayCode\(code\)/,
  'the code is shown only until the Bridge confirms it');
assert.match(settings, /if \(security_refresh_timer\) \{\s*lv_timer_del\(security_refresh_timer\);\s*security_refresh_timer = nullptr;\s*\}\s*if \(networkTransport\.isWifiDriverActive\(\)\) WiFi\.scanDelete\(\);/,
  'closing the popup deletes the refresh timer');
for (const key of ['security_bridge_encryption', 'security_encryption_waiting', 'security_encryption_setup',
  'security_encryption_new_code', 'security_encryption_turn_off', 'security_encryption_code_hint',
  'security_encryption_active_hint', 'security_encryption_off_hint']) {
  assert.match(settings, new RegExp(`tr\\(\\)\\.${key}`), `${key} comes from i18n`);
}
// The retained announcement is signed while a code exists and republished
// whenever the code changes; without pairing it stays byte-identical.
const announce = network.slice(network.indexOf('void HomeTilesNetworkManager::publishBridgeConfig() {'),
  network.indexOf('const char* HomeTilesNetworkManager::getBridgeApplyTopic()'));
assert.match(announce, /command_channel::signAnnouncement\(topic\.c_str\(\), payload\.c_str\(\), payload\.length\(\)\)/);
assert.match(announce, /is_signed \? signed_payload : payload\.c_str\(\)/);
assert.match(announce, /if \(signed_payload\) heap_caps_free\(signed_payload\);/);
assert.ok(announce.indexOf('mqttEnqueuePublishWithLargeBuffer') < announce.indexOf('heap_caps_free(signed_payload)'),
  'the signed copy is freed only after the queue copied it');
const signer = channel.slice(channel.indexOf('char* signAnnouncement(const char* topic, const char* payload, size_t length) {'));
assert.match(signer, /if \(!g_state \|\| !topic \|\| !payload \|\|\s*xTaskGetCurrentTaskHandle\(\) != g_owner\) \{\s*return nullptr;/,
  'no pairing (or a foreign task) leaves the announcement unsigned');
assert.match(signer, /allocPreferPsram\(size\)/);
const create = channel.slice(channel.indexOf('bool createCode() {'), channel.indexOf('bool disable(bool* bridge_notified) {'));
// Turning off (on the display or by the Bridge) republishes the unsigned announcement.
const off = channel.slice(channel.indexOf('bool turnOff('), channel.indexOf('void handleUnpair('));
assert.match(create, /networkManager\.publishBridgeConfig\(\);/);
assert.match(off, /releaseState\(\);[\s\S]*networkManager\.publishBridgeConfig\(\);/);

const doc = readRepoFile('docs-dev/command-encryption.md');
for (const marker of ['secure/panel', 'secure/bridge', 'stat/secure', 'HomeTiles command pairing v1',
  'panel-to-bridge', 'bridge-to-panel', 'key-id', 'announce', '"sig"', 'ChaCha20-Poly1305', 'replay']) {
  assert.ok(doc.includes(marker), `protocol document covers ${marker}`);
}
console.log('Command channel wiring: outbound sealing, inbound routing, lifecycle and UI passed');
