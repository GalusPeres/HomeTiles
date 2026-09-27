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
const doc = readRepoFile('docs-dev/command-encryption.md');
for (const marker of ['secure/panel', 'secure/bridge', 'stat/secure', 'HomeTiles command pairing v1',
  'panel-to-bridge', 'bridge-to-panel', 'key-id', 'ChaCha20-Poly1305', 'replay']) {
  assert.ok(doc.includes(marker), `protocol document covers ${marker}`);
}
console.log('Command channel wiring: outbound sealing, inbound routing, lifecycle and UI passed');
